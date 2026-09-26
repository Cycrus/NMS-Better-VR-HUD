# No Man's Sky Better VR HUD

A mod to optimize the HUD in VR mode in the game No Man's Sky. It attempts to fix the position of the HUD by attaching it to the head rotation instead of the body rotation. This way the hud is always in the view of the player without them needing to return to an artificial "forward position".

The mod utilizes a DLL side loading technique using `Ultimate ASI Loader`.

NMS.exe is reverse engineered using radare2 and cheatengine.

## TODO
- [x] Make DLL sideloading work with Ultimate ASI Loader
- [x] Hook into anything from No Man's Sky using MinHook
- [x] Figure out how to extract player camera transform
- [x] Figure out how to manipulate HUD transform
- [ ] Connect camera transform to HUD transform for "sticky" HUD view

## Mod Concept
- Mod .asi plugin is side-loaded using `Ultimate ASI Loader`.
- Camera transform matrix access
    - Matrix lives at address NMS.exe+0x6E7CA40
    - Mod hooks into address NMS.exe+0x337927 (generateCameraTransform), which is called once every frame when the camera transform is updated
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
- Camera transform matrix: NMS.exe+0x6E7CA40 (16 floats)
- applyObjectTransform method address: NMS.exe+0x1838500
- updateVROffset method address: NMS.exe+0x2C356C0
- "static" VR HUD object handle: 0x00080133