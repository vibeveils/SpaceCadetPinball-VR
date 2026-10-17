#!/usr/bin/env bash
# Build and package Space Cadet Pinball VR for Linux (x86_64 or arm64 / Steam Frame).
#
# Intended to run inside the Steam Linux Runtime 3.0 "sniper" SDK, so the result runs under
# "Steam Linux Runtime 3.0 (Sniper)" on SteamOS and on the Steam Frame:
#   x86_64: registry.gitlab.steamos.cloud/steamrt/sniper/sdk
#   arm64:  registry.gitlab.steamos.cloud/steamrt/sniper/sdk/arm64
# It also works on a normal Linux system that has the build dependencies installed
# (the binaries then need that system's glibc or newer; see MAX_GLIBC below).
#
# Usage: Platform/SteamFrame/build-linux.sh [output-dir]
# Environment:
#   BUILD_DIR   build directory (default: build-linux-<arch>)
#   MAX_GLIBC   fail if the binaries need a newer glibc (default 2.31 = sniper; "none" to skip)
#   PKG_SUFFIX  extra text for the package name, e.g. a version or commit
#   CMAKE_ARGS  extra CMake arguments, e.g. "-DFETCHCONTENT_SOURCE_DIR_OPENXR_SDK=/path/to/OpenXR-SDK"
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
OUT_DIR="${1:-$ROOT/dist}"
ARCH="$(uname -m)"
case "$ARCH" in
	aarch64|arm64) ARCH=arm64 ;;
	x86_64|amd64) ARCH=x86_64 ;;
	*) echo "Unsupported architecture: $ARCH" >&2; exit 1 ;;
esac
BUILD_DIR="${BUILD_DIR:-$ROOT/build-linux-$ARCH}"
MAX_GLIBC="${MAX_GLIBC:-2.31}"
NAME="SpaceCadetPinball-VR-linux-$ARCH${PKG_SUFFIX:+-$PKG_SUFFIX}"
STAGE="$OUT_DIR/$NAME"

echo "== Toolchain"
cmake --version | head -1
"${CXX:-c++}" --version | head -1
ldd --version 2>&1 | head -1 || true

# OpenXR SDK needs CMake 3.16+. Old images can get a newer one from PyPI.
cmake_ver="$(cmake --version | head -1 | grep -oE '[0-9]+\.[0-9]+' | head -1)"
if [ "$(printf '%s\n3.16\n' "$cmake_ver" | sort -V | head -1)" != "3.16" ]; then
	echo "CMake $cmake_ver is too old, installing a newer one with pip"
	python3 -m pip install --user --upgrade cmake
	export PATH="$HOME/.local/bin:$PATH"
fi

generator=()
command -v ninja >/dev/null && generator=(-G Ninja)

echo "== Configure"
cmake -S "$ROOT" -B "$BUILD_DIR" "${generator[@]}" \
	-DCMAKE_BUILD_TYPE=Release \
	-DSCP_VR=ON \
	-DSCP_VR_FETCH_OPENXR=ON \
	-DSCP_PORTABLE=ON \
	-DSCP_BUNDLE_SDL_MIXER=ON \
	-DCMAKE_RUNTIME_OUTPUT_DIRECTORY="$BUILD_DIR/out" \
	${CMAKE_ARGS:-}

echo "== Build"
cmake --build "$BUILD_DIR" --parallel "$(nproc 2>/dev/null || echo 2)"

echo "== Package $NAME"
rm -rf "$STAGE"
mkdir -p "$STAGE/lib"
cp "$BUILD_DIR/out/SpaceCadetPinball" "$BUILD_DIR/out/SpaceCadetPinballVRTest" "$STAGE/"
strip "$STAGE/SpaceCadetPinball" "$STAGE/SpaceCadetPinballVRTest" 2>/dev/null || true

# The OpenXR loader is not part of the Steam Runtime, so ship it next to the game (found via $ORIGIN/lib).
loader="$(find "$BUILD_DIR" -name 'libopenxr_loader.so.1*' -type f | head -1)"
if [ -z "$loader" ]; then
	echo "OpenXR loader library not found in $BUILD_DIR" >&2
	exit 1
fi
cp "$loader" "$STAGE/lib/libopenxr_loader.so.1"
strip "$STAGE/lib/libopenxr_loader.so.1" 2>/dev/null || true

cp "$ROOT/Platform/SteamFrame/run-vr.sh" "$STAGE/"
chmod +x "$STAGE/run-vr.sh"
cp "$ROOT/README.md" "$ROOT/LICENSE" "$ROOT/Platform/SteamFrame/THIRD_PARTY.txt" "$STAGE/"
mkdir -p "$STAGE/assets"
printf 'Copy the original Space Cadet game files (PINBALL.DAT, FONT.DAT, table.bmp, *.WAV, PINBALL2.MID) into this folder.\n' \
	> "$STAGE/assets/PUT_GAME_DATA_HERE.txt"

echo "== Dependencies"
for f in "$STAGE/SpaceCadetPinball" "$STAGE/lib/libopenxr_loader.so.1"; do
	echo "$(basename "$f"):"
	readelf -d "$f" | grep -E 'NEEDED|RUNPATH|RPATH' | sed 's/^/  /'
done

if [ "$MAX_GLIBC" != "none" ]; then
	echo "== glibc check (max $MAX_GLIBC)"
	needed="$(for f in "$STAGE/SpaceCadetPinball" "$STAGE/SpaceCadetPinballVRTest" "$STAGE/lib/libopenxr_loader.so.1"; do
		objdump -T "$f" | grep -oE 'GLIBC_[0-9]+\.[0-9]+(\.[0-9]+)?'
	done | sed 's/GLIBC_//' | sort -V -u | tail -1)"
	echo "Highest glibc symbol version needed: $needed"
	if [ "$(printf '%s\n%s\n' "$needed" "$MAX_GLIBC" | sort -V | tail -1)" != "$MAX_GLIBC" ]; then
		echo "ERROR: binaries need glibc $needed, newer than $MAX_GLIBC. Build inside the sniper SDK" \
			"or set MAX_GLIBC=none." >&2
		exit 1
	fi
fi

tar -C "$OUT_DIR" -czf "$OUT_DIR/$NAME.tar.gz" "$NAME"
echo "== Done: $OUT_DIR/$NAME.tar.gz"
