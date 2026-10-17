# Space Cadet Pinball in VR (Steam Frame / OpenXR)

This fork adds a VR mode. The original 2D game runs unchanged; its frame (table, score panel and menus)
is placed in a 3D scene rendered with Vulkan through OpenXR:

* **Cabinet view** (default): the playfield sits on the angled top of a pinball cabinet, the score panel
  is on the backbox, under glass with chrome rails and neon trim, in a space-themed arcade.
* **Theater view**: one large screen with the whole game, like the desktop version.

Controller rumble follows the game (flippers, bumpers, kickers, plunger, tilt), and nudging physically
shakes the cabinet.

## Controls

Bindings are provided for the Steam Frame controller (`XR_VALVE_frame_controller_interaction`), Oculus
Touch (also what the Frame emulates for older games), Valve Index, HTC Vive and the KHR simple controller.
They can be rebound in SteamVR's controller binding UI.

| Action | Steam Frame | Touch-style |
|---|---|---|
| Left / right flipper | Left / right trigger or bumper | Left / right trigger |
| Plunger (hold, release) | A | A |
| Nudge left / right / up | Left stick flick or D-pad | Left stick flick |
| Pause | B | B |
| New game | Hold X | Hold X (left) |
| Cabinet / theater view | Y | Y (left) |
| Move table nearer / further | Right stick up / down | Right stick |
| Playfield tilt (theater: screen size) | Right stick left / right | Right stick |
| Recenter | Right stick click | Right stick click |
| Menu (pauses the game) | View or Menu button | Menu (left) |
| Close menu | D-pad down or Menu again | Left stick click or Menu |

In menus, point with either controller and pull its trigger to click. The **VR** menu has sliders for
tilt, distance, height and table size, plus the controls reference. High-score names are entered with an
on-screen keyboard. Settings are saved with the game's other options.

A regular gamepad also works in both modes; its analog triggers now act as flippers too (they mirror the
shoulder buttons).

## Game data

As with the upstream project, the original game data is not included. Copy `PINBALL.DAT` and the rest of
the Space Cadet files (or the Full Tilt! `CADET.DAT` set) into the `assets` folder next to the executable:

```
SpaceCadetPinball-VR-linux-arm64/
├── SpaceCadetPinball
├── run-vr.sh
├── lib/
└── assets/          <- PINBALL.DAT, FONT.DAT, table.bmp, *.WAV, *.MID
```

The game also still finds the files directly next to the executable or in the working directory.

## Running

```
SpaceCadetPinball -vr            # VR mode (or set SCP_VR=1)
SpaceCadetPinball -vr -vrscale=3 # sharper menus/text, more CPU work (1-4, default 2)
SpaceCadetPinball                # normal desktop mode
```

An OpenXR runtime must be active (SteamVR, the Steam Frame's built-in runtime, or Monado).
If VR cannot start, the game shows an error and exits instead of falling back to the desktop.

### Steam Frame, standalone (native ARM64)

1. On the headset: **Settings > System > Developer Mode**, then **Settings > Developer > Pair new host**.
2. On a PC, open the **SteamOS Devkit Client**, register and connect to the Frame.
3. **Title Upload**:
   * Local Folder: the unpacked `SpaceCadetPinball-VR-linux-arm64` folder, **with the game files in its
     `assets` folder**
   * Start Command: `run-vr.sh`
   * Runtime: **Steam Linux Runtime 3.0 ARM64 (Sniper)**
4. Upload, then **Start**. It also appears under *Library > Non-Steam*.

### Steam Frame streaming from a PC

Run the x86_64 Linux build on a PC with SteamVR and stream to the Frame as you would any PC VR game
(`SpaceCadetPinball -vr`, or add it as a non-Steam game with launch option `-vr`).

## Building

Requirements on top of the upstream ones: Vulkan headers (`vulkan-headers` / `libvulkan-dev`) and an
OpenXR loader (`openxr-sdk`, or let CMake download it with `SCP_VR_FETCH_OPENXR=ON`, the default).

```
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

Options:

* `-DSCP_VR=OFF` builds the plain desktop game.
* `-DOPENXR_INCLUDE_DIR=… -DOPENXR_LIBRARY=…` uses a specific OpenXR loader.
* `-DVULKAN_HEADERS_DIR=…` points at Vulkan headers. The game loads Vulkan at run time, so no Vulkan
  library is needed at build time and the desktop mode still works without Vulkan.

For a self-contained Linux ARM64 package, build on (or cross-compile for) aarch64 and put
`libSDL2-2.0.so.0`, `libSDL2_mixer-2.0.so.0` and `libopenxr_loader.so.1` in a `lib/` folder next to the
executable; the binary looks there first (`$ORIGIN/lib`).

`SpaceCadetPinballVRTest` renders the VR scene with a synthetic table and needs no game data, which is
handy for checking a headset/runtime setup.

The scene shaders are pre-compiled into `vr/vr_shaders.h`. After editing `vr/shaders/*`, regenerate with
`python3 SpaceCadetPinball/vr/shaders/compile_shaders.py` (needs `glslangValidator`).

### GitHub Actions

`.github/workflows/SteamFrameVR.yml` builds Linux x86_64 and arm64 (Steam Frame) packages on every
push and pull request, inside Valve's Steam Linux Runtime 3.0 "sniper" SDK containers
(`registry.gitlab.steamos.cloud/steamrt/sniper/sdk` and `.../sdk/arm64`). Download them from the run's
**Artifacts**. Pushing a tag such as `v1.0.0` also creates a GitHub release with both packages attached.
The arm64 job uses GitHub's `ubuntu-24.04-arm` runner, which is free for public repositories.

Packaged builds link SDL2_mixer statically (`-DSCP_BUNDLE_SDL_MIXER=ON`, WAV + MIDI only), because the
Steam Linux Runtime 3.0 ARM64 provides SDL2 but not SDL2_mixer. MIDI music needs Timidity instrument
patches, which SteamOS doesn't ship, so music stays silent there (it is off by default); sound effects work.

The same packaging runs locally with `Platform/SteamFrame/build-linux.sh [output-dir]`. It checks that
the binaries need no newer glibc than the runtime's 2.31 (set `MAX_GLIBC=none` to skip this outside the
SDK).

### Debug environment variables

* `SCP_VR_EYE_SIZE=1600x1600` overrides the per-eye render size.
* `SCP_VR_SCREENSHOT=/path/eye.ppm` and `SCP_VR_SCREENSHOT_FRAME=N` save the left eye on frame N.
* `SCP_VR_DEBUG_PITCH=30` tilts the rendered view down (for screenshots on simulated headsets).

## How it works

* `winmain.cpp` – in VR mode the game renders with SDL's software renderer into an off-screen surface
  (2x the table resolution), using SDL's `offscreen` video driver, and runs a VR main loop paced by
  `xrWaitFrame`. Physics runs at the configured update rate in fixed steps.
* `vr/VrRuntime.cpp` – OpenXR instance/session (`XR_KHR_vulkan_enable2`), action bindings, Vulkan
  renderer (4x MSAA with lazily allocated attachments for tile-based GPUs, mip-mapped and anisotropically
  filtered game texture so the tilted playfield stays sharp), the pointer, haptics and table placement.
* `vr/VrScene.cpp`, `vr/shaders` – procedural cabinet and environment geometry and a single uber-shader.
* `vr/VrFeedback.cpp` – maps the game's sound events to controller rumble.
