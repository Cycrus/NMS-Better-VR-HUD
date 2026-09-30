<div align="center">
  <img src="resources/logo.png" alt="NMS Better VR HUD logo" width="250">
  <h1>No Man's Sky Better VR HUD</h1>
  <h2>An optimized VR HUD system mod for No Man's Sky</h2>
</div>

<div align="center">
  <img alt="License" src=https://img.shields.io/badge/License-MIT-green?style=flat-square>
  <img alt="OS" src=https://img.shields.io/badge/OS-Windows-yellow?style=flat-square>
  <img alt="Platforms" src=https://img.shields.io/badge/Platforms-x86__64-blue?style=flat-square>
  <img alt="Languages" src=https://img.shields.io/badge/Languages-C++-red?style=flat-square>
</div>

# No Man's Sky Better VR HUD

Written for No Man's Sky 7.04 (Cosmos Update).

A mod to optimize the HUD in VR mode in the game No Man's Sky. It attempts to fix the position of the HUD by attaching it to the head rotation instead of the body rotation. This way the hud is always in the view of the player without them needing to return to an artificial "forward position".

The mod utilizes a DLL side loading technique using `Ultimate ASI Loader`.

For this mod NMS.exe is partially reverse engineered and analyzed using radare2 and cheatengine.

There are two versions of the mod. Pick one to your liking:
- BetterVrHudFull.asi - The HUD moves on every axis with the head and "sticks" to it even when you look up or down.
- BetterVrHudLevel.asi - The HUD rotates only on the z axis around the player.

This repo uses submodules. Please clone it with:
```bash
git clone --recurse-submodules git@github.com:Cycrus/NMS-Better-VR-HUD.git
```

## Showcase
### Default VR HUD
No mod installed.

https://github.com/user-attachments/assets/8de84137-206e-45a7-b363-7ad6d3a4327c

### Better VR HUD Full
Full camera view tracking hud.

https://github.com/user-attachments/assets/03ab7285-8073-408a-ba28-064619f5c3b7

### Better VR HUD Level
Only rotates on z axis around player.

https://github.com/user-attachments/assets/8a45316e-917d-485e-92ab-0fa2fd015f0a

## Installation
The [Latest main build](https://github.com/Cycrus/NMS-Better-VR-HUD/releases/tag/continuous) is automatically updated after every successful build on `main`. Both `.asi` variants are also available as artifacts from the corresponding GitHub Actions run.

1. Download the file `winmm.dll` from the [Ultimate ASI Loader page](https://github.com/ThirteenAG/Ultimate-ASI-Loader/releases) and place it into the same directory as NMS.exe.
2. Create a directory called `plugins` in the same directory as NMS.exe.
3. Copy either `BetterVrHudFull.asi` or `BetterVrHudLevel.asi` into the plugins directory (DO NOT COPY BOTH!).
4. Start the game and enjoy.

### Linux Note
On Linux when you use Proton, you need to set those launch options for the game:

```
WINEDLLOVERRIDES="winmm=n,b" %command%
```

## Mod Concept
- Mod .asi plugin is side-loaded using `Ultimate ASI Loader`.
- Camera transform matrix access
    - Matrix lives at address NMS.exe+0x6E7CA40
    - Mod hooks into address NMS.exe+0x337927 (generateVRTransforms), which is called once every frame when the VR transforms are updated
- Body transform matrix access
    - Matrix lives at address NMS.exe+0x6E7CAD0
    - Mod hooks into address NMS.exe+0x337785 (generateVRTransforms), which is called once every frame when the VR transforms are updated
- HUD transform matrix access
    - Mod calls code address NMS.exe+0x1838500 (applyObjectTransform)
    - applyObjectTransform is called from NMS.exe+0x2C356C0 (updateVROffset), which is called once every frame when the VR HMD transform is updated
    - It uses the object handle 0x00080133 to reference the HUD.
- Addresses are not hooked statically, but are searched for by unique opcode signatures.
