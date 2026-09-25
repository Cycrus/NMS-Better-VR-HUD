# No Man's Sky Better VR HUD

A mod to optimize the HUD in VR mode in the game No Man's Sky. It attempts to fix the position of the HUD by attaching it to the head rotation instead of the body rotation. This way the hud is always in the view of the player without them needing to return to an artificial "forward position".

The mod utilizes a DLL side loading technique using `Ultimate ASI Loader`.

It works in Windows and Linux. In Linux under proton you need the following Steam launch options for the game: `WINEDLLOVERRIDES="winmm=n,b" %command%`.

## Status

The main HUD panel follows the head, on foot and in the ship cockpit. Menus are left where the game puts them.

Tested on Windows 11 with a Quest 3 through Virtual Desktop and SteamVR, NMS.exe built 2026-09-21 (PE timestamp `0x6AB0FFC9`).

## Installation

1. Download the x64 build of [Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader/releases), rename its `dinput8.dll` to `winmm.dll` and copy it to `No Man's Sky\Binaries`.
2. Copy `BetterVrHud.asi` to `No Man's Sky\Binaries`.
3. Linux/Proton only: set the launch options above.

To uninstall, delete `BetterVrHud.asi` (and `winmm.dll` if nothing else uses it).

## Usage

The HUD is head-locked when the game starts. `F8` switches between head-locked and the game's default.

On first run the mod creates `BetterVrHud.ini` next to the `.asi`:

```ini
[BetterVrHud]
; 1 keeps the HUD in front of your eyes, 0 leaves the game default.
HeadLock=1
; Key that switches HeadLock in game: F1-F24, a virtual-key code such as 0x77, or 0 for none.
ToggleKey=F8
```

`BetterVrHud.log` records startup and every change of lock status. All game code is located by byte signatures; if a game update changes it, the log says so and the HUD is left unchanged.

## Building

Needs mingw-w64 and the MinHook submodule:

```sh
git clone --recurse-submodules https://github.com/Cycrus/NMS-Better-VR-HUD.git
cd NMS-Better-VR-HUD
make            # Linux, with x86_64-w64-mingw32-g++
mingw32-make    # Windows, e.g. WinLibs: winget install BrechtSanders.WinLibs.POSIX.UCRT
```

This produces `build/BetterVrHud.asi` and `build/FrameProbe.asi`. `make tests` builds `build/mat4_test.exe` (matrix math) and `build/resolve_test.exe`, which checks every signature against an `NMS.exe` given as its argument.

## How it works

The game sets the HUD panel's local matrix every frame, relative to an anchor that follows the head position but not its rotation. The mod hooks that call and replaces the matrix so that the layout the game shows when looking straight ahead turns with the head:

    local = offset * anchor * inverse(N) * camera * inverse(anchor)

where `N` is the anchor's orientation placed at the eyes. The camera is updated before the HUD in each frame, so the replaced matrix uses the same camera as the rendered frame.

See [docs/reverse-engineering.md](docs/reverse-engineering.md) for the engine structures, addresses and measurements behind this.

## Repository layout

| Path | Contents |
|---|---|
| `src/common` | Shared code: signatures and engine structures (`nms`), head-lock math (`hud_lock`, `mat4.h`), MinHook wrapper, logging, OpenVR pose hook |
| `src/mod` | `BetterVrHud.asi` |
| `tools/FrameProbe` | Diagnostic `.asi` that logs HUD nodes, anchors, camera and HMD pose per frame to CSV (`F8` toggles the lock, `F9` writes a marker). Install instead of the mod, not alongside it |
| `tools/analysis` | Python loader for FrameProbe CSVs |
| `tools/tests` | Math and signature tests |
| `third_party/minhook` | MinHook submodule |
| `CameraTransformLogger`, `HmdTransformLogger`, `HudContinuousTransform`, `HudProber`, `HudTransformTest`, `OpenVrLogger`, `Testmod` | Original research prototypes |

Research commands:

    r2 -A NMS.exe --> Static analysis of program
    r2 -d <pid> --> debug analysis of process

## TODO
- [x] Make DLL sideloading work with Ultimate ASI Loader
- [x] Hook into anything from No Man's Sky using MinHook
- [x] Figure out where the HMD/camera transform is stored (camera world matrix of the active view)
- [x] Figure out where the HUD position is set (HUD builder, applied to a scene node by handle)
- [x] Head-lock the main HUD on foot and in the ship cockpit
- [ ] Turn the VR body with the head: with the game's "show body" option on, the body keeps facing the old forward direction and can block the view
- [ ] Optional lazy-follow / smoothing mode
- [ ] HUD distance and offset settings
- [ ] Test exocraft, space stations, freighters and photo mode
- [ ] Test under Linux/Proton
- [ ] CI build and release package
