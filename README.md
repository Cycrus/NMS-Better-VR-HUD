# No Man's Sky Better VR HUD

A mod to optimize the HUD in VR mode in the game No Man's Sky. It attempts to fix the position of the HUD by attaching it to the head rotation instead of the body rotation. This way the hud is always in the view of the player without them needing to return to an artificial "forward position".

The mod utilizes a DLL side loading technique using `Ultimate ASI Loader`.

It works in Windows and Linux. In Linux under proton you need the following Steam launch options for the game: `WINEDLLOVERRIDES="winmm=n,b" %command%`.

## TODO
- [x] Make DLL sideloading work with Ultimate ASI Loader
- [ ] Hook into anything from No Man's Sky using MinHook
- [ ] Figure out where HMD rotation matrix is stored using radare2 and cheatengine
- [ ] Figure out how to extract HMD rotation matrix using radare2 and cheatengine
- [ ] Figure out where individual HUD elements position is stored using radare2 and cheatengine
- [ ] Figure out how to manipulate individual HUD elements positions using radare2 and cheatengine