# Building and developing Space Cadet Pinball VR

Targets: Linux **arm64** (Steam Frame, standalone) and Linux **x86_64** (PCs with SteamVR or Monado).

## Project layout

| Path | What it is |
|---|---|
| `SpaceCadetPinball/` | The decompiled game (from upstream), plus small hooks for VR |
| `SpaceCadetPinball/vr/` | The VR layer: OpenXR, Vulkan renderer, cabinet scene, input, haptics |
| `SpaceCadetPinball/vr/shaders/` | GLSL sources of the scene shader and the script that embeds them |
| `Platform/SteamFrame/` | Packaging script, launcher (`run-vr.sh`) and third-party notice |
| `tools/extract_assets.py` | Exports the game's graphics from `PINBALL.DAT` to PNG |
| `Doc/dat-file-format.txt` | Description of the `.DAT` resource format |
| `.github/workflows/SteamFrameVR.yml` | CI: builds and packages both architectures |

## Building

Requirements: CMake 3.16+, a C++14 compiler, SDL2 development files, Vulkan headers, Python 3, and
either SDL2_mixer development files or network access (see `SCP_BUNDLE_SDL_MIXER`). The OpenXR loader is
downloaded and built automatically if it isn't installed.

On Debian / Ubuntu:

```
sudo apt install cmake ninja-build g++ libsdl2-dev libsdl2-mixer-dev libvulkan-dev python3
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

The executables are written to `bin/`. Put the game files in `bin/assets/` and run
`bin/SpaceCadetPinball -vr`.

### CMake options

| Option | Default | Meaning |
|---|---|---|
| `SCP_VR` | `ON` | Build the VR mode. `OFF` gives the plain desktop game. |
| `SCP_VR_FETCH_OPENXR` | `ON` | Download and build the OpenXR loader when it isn't found on the system. |
| `SCP_BUNDLE_SDL_MIXER` | `OFF` | Build SDL2_mixer from source (WAV + MIDI only) and link it statically. Needed for the Steam Frame, whose runtime has SDL2 but not SDL2_mixer. |
| `SCP_PORTABLE` | `OFF` | Look for libraries only in `$ORIGIN/lib` and `$ORIGIN`, for redistributable packages. |
| `OPENXR_INCLUDE_DIR`, `OPENXR_LIBRARY` | | Use a specific OpenXR loader. |
| `VULKAN_HEADERS_DIR` | | Location of `vulkan/vulkan.h`. Vulkan itself is loaded at run time, so no Vulkan library is needed to build, and desktop mode works without Vulkan. |

### Packages

`Platform/SteamFrame/build-linux.sh [output-dir]` builds and packages for the machine's architecture
with the options above set for redistribution. The package contains the game, `run-vr.sh`,
`lib/libopenxr_loader.so.1`, an empty `assets/` folder, `README.md`, the licence and the third-party
notice. The script fails if the binaries need a newer glibc than 2.31, the version in Steam Linux
Runtime 3.0 (set `MAX_GLIBC=none` to skip this check when building outside the runtime SDK).

### GitHub Actions

`.github/workflows/SteamFrameVR.yml` runs `build-linux.sh` on every push and pull request, inside
Valve's Steam Linux Runtime 3.0 "sniper" SDK containers:

- x86_64: `registry.gitlab.steamos.cloud/steamrt/sniper/sdk` on `ubuntu-24.04`
- arm64: `registry.gitlab.steamos.cloud/steamrt/sniper/sdk/arm64` on `ubuntu-24.04-arm` (free for
  public repositories)

The packages are uploaded as the run's **Artifacts**. Pushing a tag such as `v1.0.0` also publishes them
on a GitHub release.

## Running

```
SpaceCadetPinball -vr              # VR mode (or set SCP_VR=1)
SpaceCadetPinball -vr -vrscale=3   # render the 2D frame at 3x (1-4, default 2): sharper text, more CPU
SpaceCadetPinball                  # desktop mode, useful for checking game logic
```

The game looks for its data in the working directory, `assets/`, next to the executable, and
`<executable folder>/assets/`.

## Testing without a headset

Monado can run a simulated headset with no display, which is enough to run the full game, check the
log and capture what each eye sees:

```
sudo apt install monado-service libopenxr1-monado
# The service needs an open pipe on stdin and exits when it closes.
sleep infinity | XRT_COMPOSITOR_NULL=1 SIMULATED_ENABLE=1 SIMULATED_LEFT=simple SIMULATED_RIGHT=simple \
    monado-service &
export XR_RUNTIME_JSON=/usr/share/openxr/1/openxr_monado.json

bin/SpaceCadetPinballVRTest 60          # synthetic table, no game data needed
SCP_VR_EYE_SIZE=900x900 SCP_VR_DEBUG_PITCH=30 \
SCP_VR_SCREENSHOT=/tmp/eye.ppm SCP_VR_SCREENSHOT_FRAME=60 bin/SpaceCadetPinball -vr -demo
```

Debug environment variables:

| Variable | Effect |
|---|---|
| `SCP_VR_EYE_SIZE=WxH` | Override the per-eye render size. |
| `SCP_VR_SCREENSHOT=file.ppm`, `SCP_VR_SCREENSHOT_FRAME=N` | Save the left eye on frame N. |
| `SCP_VR_DEBUG_PITCH=degrees` | Tilt the rendered view down, to look at the table from a simulated headset. |

The simulated controllers don't press buttons, so grip and trigger interactions need a real headset or
temporary scripted input.

## How the VR mode works

- **Frame capture** (`winmain.cpp`): in VR mode the game draws with SDL's software renderer into an
  off-screen surface at 2× the table resolution, using SDL's `offscreen` video driver. `VrMainLoop` is
  paced by `xrWaitFrame` and steps the physics at the configured update rate. While a menu is open, a
  copy of the frame without menus is kept for the cabinet's screens.
- **Un-projecting the table** (`winmain.cpp`, `VrUpdateArtworkLayout`): the table art is a perspective
  rendering, and the game keeps the camera it was rendered with (`proj`). The painted frame's edges are
  detected in the image, and a flat rectangle of the same proportions is mapped back onto the image
  through that camera, so VR supplies the perspective.
- **Renderer** (`vr/VrRuntime.cpp`): OpenXR with `XR_KHR_vulkan_enable2`, Vulkan with 4× MSAA on
  lazily allocated attachments (cheap on the Frame's tile-based GPU), and a mip-mapped, anisotropically
  filtered frame texture so the tilted playfield stays sharp. Menus and the pointer are drawn in a final
  pass without depth testing, so the cabinet never covers them.
- **Scene** (`vr/VrScene.cpp`, `vr/shaders/`): procedural cabinet, environment and controller geometry
  with a single shader. The SPIR-V is embedded in `vr/vr_shaders.h`; after editing the shaders, run
  `python3 SpaceCadetPinball/vr/shaders/compile_shaders.py` (needs `glslangValidator`).
- **Input** (`vr/VrRuntime.cpp`): OpenXR actions with bindings for the Steam Frame controller
  (`XR_VALVE_frame_controller_interaction`), Touch, Index, Vive and the KHR simple controller. Grips
  drive the cabinet's flipper buttons and plunger during play, and move the table while paused.
- **Physical plunger** (`TPlunger.cpp`): `TPlunger::AnalogPull` lets the pull distance set the
  plunger's charge directly, instead of charging over time.
- **Haptics** (`vr/VrFeedback.cpp`): controller rumble derived from the game's sound events.

## Graphics

`tools/extract_assets.py PINBALL.DAT -o out` exports every bitmap as an RGBA PNG (colour index 0 =
transparent), with `manifest.json` describing where each one is drawn. Add `--zmaps` to also export the
depth maps that let the ball pass behind ramps.
