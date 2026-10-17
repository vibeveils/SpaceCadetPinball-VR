#!/bin/sh
# Launcher for Space Cadet Pinball VR on Steam Frame (or any Linux OpenXR setup).
# Use this as the "Start Command" in the SteamOS Devkit Client.
# Output goes to last-run.log next to this script, for troubleshooting.
HERE="$(cd "$(dirname "$0")" && pwd)"
cd "$HERE" || exit 1

# Uploads from Windows can lose the executable bit.
chmod +x "$HERE/SpaceCadetPinball" "$HERE/SpaceCadetPinballVRTest" 2>/dev/null

export LD_LIBRARY_PATH="$HERE/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
{
	echo "=== $(date) $(uname -m)"
	echo "args: -vr $*"
} > "$HERE/last-run.log" 2>/dev/null
exec "$HERE/SpaceCadetPinball" -vr "$@" >> "$HERE/last-run.log" 2>&1
