#!/bin/sh
# Launcher for Space Cadet Pinball VR on Steam Frame (or any Linux OpenXR setup).
# Use this as the "Start Command" in the SteamOS Devkit Client.
HERE="$(cd "$(dirname "$0")" && pwd)"
cd "$HERE" || exit 1
export LD_LIBRARY_PATH="$HERE/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
exec "$HERE/SpaceCadetPinball" -vr "$@"
