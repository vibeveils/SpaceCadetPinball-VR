# Contributing

This fork is about playing Space Cadet Pinball in VR, on the Steam Frame and Linux OpenXR headsets.
Changes to the decompiled game itself that aren't needed for VR belong in the upstream project,
[k4zmu2a/SpaceCadetPinball](https://github.com/k4zmu2a/SpaceCadetPinball).

## Reporting issues

Please include:

- the headset and controllers, and whether the game ran on the Steam Frame itself or on a PC (and which
  OpenXR runtime: SteamVR, Monado, …)
- the build: release version, or the commit / Actions run it came from
- which game data you use (`PINBALL.DAT` or Full Tilt `CADET.DAT`)
- `last-run.log` from the game folder, and what you saw or expected

## Pull requests

- Keep changes to the original game code small and limited to what VR needs; put VR code in
  `SpaceCadetPinball/vr/` where possible.
- The desktop build (`-DSCP_VR=OFF`) must still compile.
- Match the surrounding style: tabs for indentation, braces on their own lines.
- If you change `vr/shaders/*`, regenerate `vr/vr_shaders.h` with
  `python3 SpaceCadetPinball/vr/shaders/compile_shaders.py` and commit both.
- Test with the simulated headset described in [Doc/BUILDING.md](Doc/BUILDING.md#testing-without-a-headset),
  and on a real headset for anything involving controllers or comfort.
- CI must pass: it builds the x86_64 and arm64 packages in the Steam Linux Runtime SDK.
- Never commit the original game data (`*.DAT`, `*.WAV`, `*.MID`) or graphics extracted from it.

There's no guarantee a particular pull request will be accepted. For bigger changes, open an issue first.
