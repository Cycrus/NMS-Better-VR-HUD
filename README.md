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

This repo uses submodules. Please clone it with:
```bash
git clone --recurse-submodules git@github.com:Cycrus/NMS-Better-VR-HUD.git
```

## Installation
1. Download the file `winmm.dll` from the [Ultimate ASI Loader page](https://github.com/ThirteenAG/Ultimate-ASI-Loader/releases) and place it into the same directory as NMS.exe.
2. Create a directory called `plugins` in the same directory as NMS.exe.
3. Copy the file BetterVrHud.asi into the plugins directory.
4. Start the game and enjoy.

## TODO
- [x] Make DLL sideloading work with Ultimate ASI Loader
- [x] Hook into anything from No Man's Sky using MinHook
- [x] Figure out how to extract player camera transform
- [x] Figure out how to manipulate HUD transform
- [ ] Figure out how to extract player body transform
- [ ] Connect camera transform to HUD transform for "sticky" HUD view

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

## Findings
### Approximate default HUD transform matrix
$$
\begin{bmatrix}
1 & 0 & 0 & 0 \\
0 & 1 & 0 & 0 \\
0 & 0 & 1 & 0 \\
0 & 0 & -2.5 & 1
\end{bmatrix}
$$

### Important values
- generateVRTransforms method address: NMS.exe+0x337927
- Camera transform matrix: NMS.exe+0x6E7CA40 (16 floats)
- applyObjectTransform method address: NMS.exe+0x1838500
- updateVROffset method address: NMS.exe+0x2C356C0
- "static" VR HUD object handle: 0x00080133