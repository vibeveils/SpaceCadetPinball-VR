#!/usr/bin/env python3
"""Extract the graphics from Space Cadet Pinball data files (PINBALL.DAT, CADET.DAT, FONT.DAT ...).

Every 8-bit bitmap is converted with the table palette to an RGBA PNG (palette index 0 = transparent
for sprites; the full-screen background is exported opaque). Identical images are written once.
manifest.json records every place each image is used (group, entry, size, screen position), so
higher-resolution replacements can be mapped back into the game.

Usage: extract_assets.py PINBALL.DAT [FONT.DAT ...] -o OUTPUT_DIR [--zmaps]
Requires Pillow (pip install pillow).
"""
import argparse
import hashlib
import json
import os
import re
import struct
import sys

from PIL import Image

FIELD_SIZES = [2, -1, 2, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 0]
T_SHORT, T_BITMAP8, T_GROUPNAME, T_PALETTE, T_STRING, T_SHORTARRAY, T_FLOATARRAY, T_BITMAP16 = 0, 1, 3, 5, 9, 10, 11, 12
FLAG_UNALIGNED, FLAG_DIB, FLAG_SPLICED = 1, 2, 4

# Windows system colours the game puts in the first palette entries (gdrv::display_palette).
SYSTEM_COLORS = [(0, 0, 0), (0x80, 0, 0), (0, 0x80, 0), (0x80, 0x80, 0), (0, 0, 0x80), (0x80, 0, 0x80),
                 (0, 0x80, 0x80), (0xC0, 0xC0, 0xC0), (0xC0, 0xDC, 0xC0), (0xA6, 0xCA, 0xF0)]


def read_dat(path):
    data = open(path, "rb").read()
    sig, app, desc, file_size, n_groups, body_size, unknown = struct.unpack_from("<21s50s100siHiH", data, 0)
    if not sig.startswith(b"PARTOUT(4.0)RESOURCE"):
        raise ValueError("%s is not a PARTOUT(4.0) resource file" % path)
    pos = 183 + unknown
    full_tilt = "cadet" in os.path.basename(path).lower() or "demo" in os.path.basename(path).lower()
    groups = []
    for gi in range(n_groups):
        count = data[pos]
        pos += 1
        entries = []
        for _ in range(count):
            etype = data[pos]
            pos += 1
            size = FIELD_SIZES[etype] if etype < len(FIELD_SIZES) and FIELD_SIZES[etype] >= 0 else None
            if size is None:
                size = struct.unpack_from("<I", data, pos)[0]
                pos += 4
            raw = data[pos:pos + size]
            pos += size
            entries.append((etype, raw))
        name = ""
        for etype, raw in entries:
            if etype == T_GROUPNAME:
                name = raw.split(b"\0")[0].decode("latin-1")
        groups.append({"index": gi, "name": name, "entries": entries})
    return {"app": app.split(b"\0")[0].decode("latin-1"), "groups": groups, "full_tilt": full_tilt}


def build_palette(dat):
    raw = None
    for g in dat["groups"]:
        if g["name"] == "background":
            for etype, data in g["entries"]:
                if etype == T_PALETTE:
                    raw = data
    if raw is None:
        for g in dat["groups"]:
            for etype, data in g["entries"]:
                if etype == T_PALETTE and raw is None:
                    raw = data
    pal = [(0, 0, 0)] * 256
    for i, c in enumerate(SYSTEM_COLORS):
        pal[i] = c
    if raw:
        for i in range(10, 246):
            b, g, r, _ = raw[i * 4:i * 4 + 4]
            pal[i] = (r, g, b)
    pal[255] = (255, 255, 255)
    return pal


def decode_bitmap(raw, palette, opaque):
    res, w, h, x, y, size, flags = struct.unpack_from("<BhhhhiB", raw, 0)
    if flags & FLAG_SPLICED:
        return None, (res, w, h, x, y, flags)
    stride = w if w % 4 == 0 else w - w % 4 + 4
    pix = raw[14:14 + size]
    img = Image.new("RGBA", (w, h))
    out = []
    for row in range(h):
        src = pix[(h - 1 - row) * stride:(h - 1 - row) * stride + w]  # stored bottom-up
        for idx in src:
            r, g, b = palette[idx]
            out.append((r, g, b, 255 if (opaque or idx != 0) else 0))
    img.putdata(out)
    return img, (res, w, h, x, y, flags)


def decode_zmap(raw, full_tilt):
    off = 1 if full_tilt else 0
    w, h, stride = struct.unpack_from("<hhh", raw, off)
    body = raw[off + 14:]
    if w <= 0 or h <= 0 or stride * h * 2 != len(body):
        return None
    img = Image.new("I;16", (w, h))
    vals = struct.unpack("<%dH" % (stride * h), body)
    img.putdata([vals[(h - 1 - r) * stride + c] for r in range(h) for c in range(w)])
    return img


def safe(name):
    name = re.sub(r"[^A-Za-z0-9_.-]+", "_", name).strip("_")
    return name or "unnamed"


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("dat", nargs="+")
    ap.add_argument("-o", "--output", required=True)
    ap.add_argument("--zmaps", action="store_true", help="also export depth maps (16-bit PNG, reference only)")
    args = ap.parse_args()

    os.makedirs(os.path.join(args.output, "images"), exist_ok=True)
    manifest = {"images": {}, "sources": []}
    by_hash = {}
    total = skipped = 0

    for path in args.dat:
        dat = read_dat(path)
        palette = build_palette(dat)
        src = os.path.basename(path)
        manifest["sources"].append({"file": src, "app": dat["app"], "groups": len(dat["groups"])})
        prefix = "" if len(args.dat) == 1 else os.path.splitext(src)[0].lower() + "_"
        base_name, base_index = "unnamed", 0
        for g in dat["groups"]:
            bmp_n = 0
            # Unnamed groups hold extra frames/states of the last named part (e.g. ball frames after "ball").
            if g["name"]:
                base_name, base_index = g["name"], g["index"]
                label = safe(g["name"])
            else:
                label = "%s+%d" % (safe(base_name), g["index"] - base_index)
            for ei, (etype, raw) in enumerate(g["entries"]):
                if etype == T_BITMAP8:
                    opaque = g["name"] == "background"
                    img, (res, w, h, x, y, flags) = decode_bitmap(raw, palette, opaque)
                    total += 1
                    if img is None:
                        skipped += 1
                        continue
                    key = hashlib.sha1(img.tobytes() + struct.pack("<hh", w, h)).hexdigest()
                    use = {"source": src, "group": g["index"], "group_name": g["name"], "entry": ei,
                           "resolution": res, "x": x, "y": y}
                    if key not in by_hash:
                        fname = "%s%03d_%s%s.png" % (prefix, g["index"], label, "_%d" % bmp_n if bmp_n else "")
                        img.save(os.path.join(args.output, "images", fname), optimize=True)
                        by_hash[key] = fname
                        manifest["images"][fname] = {"width": w, "height": h, "transparent": not opaque,
                                                     "sha1": key, "uses": []}
                    manifest["images"][by_hash[key]]["uses"].append(use)
                    bmp_n += 1
                elif etype == T_BITMAP16 and args.zmaps:
                    z = decode_zmap(raw, dat["full_tilt"])
                    if z is not None:
                        os.makedirs(os.path.join(args.output, "zmaps"), exist_ok=True)
                        z.save(os.path.join(args.output, "zmaps", "%s%03d_%s_e%d.png" % (
                            prefix, g["index"], label, ei)))

    with open(os.path.join(args.output, "manifest.json"), "w") as f:
        json.dump(manifest, f, indent=1)
    print("bitmaps: %d, unique images written: %d, skipped (spliced): %d" % (total, len(by_hash), skipped))


if __name__ == "__main__":
    sys.exit(main())
