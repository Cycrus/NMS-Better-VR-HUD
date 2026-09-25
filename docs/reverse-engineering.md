# Reverse-engineering notes

Findings behind the head-locked HUD. Addresses are RVAs (offsets from the `NMS.exe` image base) for the
build with PE timestamp `0x6AB0FFC9` (2026-09-21). The mod finds everything through the signatures in
[`src/common/nms.cpp`](../src/common/nms.cpp); `build/resolve_test.exe <NMS.exe>` checks them against a binary.

Measurements come from FrameProbe logs recorded on a Quest 3 (Virtual Desktop, SteamVR).

## Matrices

The engine uses row-major 4x4 float matrices with row vectors (`v' = v * M`): rows 0-2 are the axes,
row 3 the translation, and `A * B` applies `A` first. Multiplies only use the 3x4 affine part. Column 3
carries unrelated values: the HUD panel's local matrix has `(1.2, 1.2, 1.2, 2.5)` there, i.e. its
scale and distance. Treating column 3 as homogeneous gives nonsense.

## Scene graph

`ApplyMatrix(uint32 handle, const float* matrix)` at `0x1838500` sets a node's local matrix. It checks
the handle, looks the node up and tail-jumps to the node setter at `0x18D12D0`, which ends in
`0x18DBA50` (stores current and previous local matrix) and `0x18DD0B0` (marks the subtree dirty).

Handles are `generation << 19 | index`; index `0x7FFFF` and generation 0 are invalid.

The scene manager is a global pointer loaded at `ApplyMatrix + 0x22` (`0x6E0D098`). Its arrays are
indexed by slot:

| Offset | Contents |
|---|---|
| `+0x48` | local matrix per slot (float[16]) |
| `+0x50` | previous frame's local matrix |
| `+0x60` | cached world matrix |
| `+0x70` | hierarchy, 20 bytes per slot: `+4` parent handle, `+8` first child slot, `+0x10` next sibling slot |
| `+0x78` | dirty flags (uint8) |
| `+0x90` | node object pointer; the node's handle is at `node + 8` |
| `+0xA0` | node type (uint8) |
| `+0xD0` | frame of last local write (int32) |
| `+0xE8` | handle index → slot (int32) |
| `+0x240` | frame counter |

`world = local * parentWorld` holds to about 1 cm (affine part only).

## HUD placement

`HudBuilder(object, distance, 0, scale)` at `0xAB1A20` builds a local matrix and calls `ApplyMatrix` at
`0xAB1C9D` (return address `0xAB1CA2`). It picks the node handle at `object + 0x10` or `object + 0x14`,
depending on whether the active view is `"APPVIEW"`.

Callers reach it through tail-jumping wrappers, so the builder's return address identifies the caller:

| Builder caller | Node | Notes |
|---|---|---|
| `0x932D08` (via wrapper `0x92DB40`) | main HUD panel | the wrapper reads the HUD distance/scale settings |
| `0x932B11`, `0x932C60`, `0x932CE1` (via `0x92DAE0`) | game menu panel | 0.4 m from its own anchor (`0x800E7`); head-locking it puts the menu in your face |
| `0x937A3F` | panel at 1.0 m | seen only around loading screens |
| `0x92F574` | loading-screen panel | 2.7 m, parented directly to the root |

The node handles are runtime values; in the recorded sessions the main HUD was `0x80133`, the menu
panel `0x80115`, the 1.0 m panel `0x80124` and the loading-screen panel `0x8004F`.

## Anchors and the floating origin

The main HUD's chain was `0x80133 <- 0x800EA <- 0x80089 <- 0x80088 <- 0x80000`:

- `0x80089` is a floating-origin node: a pure translation in whole multiples of 1024 m, for example
  `(-10240, 6144, 142336)`.
- `0x800EA` is the HUD anchor. Its local matrix is relative to the origin node, i.e. in the same space
  as the camera.
  - On foot it sits at the eyes (median 0.03-0.09 m away) with the tracking-space orientation: it
    turns with snap/smooth turning but not with the head. The camera equals head rotation × anchor to
    within about 2.6°.
  - In the ship cockpit it sits about 0.7 m in front of the eyes, aligned with the cockpit, scaled to
    0.2475. The game offset is `(0, 0, -0.3)` at scale 1.4, which puts the HUD about 0.75 m from the eyes.
- The game offset on foot is `(0, 0, -2.5)` at scale 1.2.

## Camera

The HUD builder loads a global "active view object" pointer at `HudBuilder + 0x51` (`0x6E7AAC0`). The
camera world matrix is at `view + 0x510`: unit axes in rows 0-2 and the eye position in row 3, in the
origin-relative space. For the default view object (`0x6E7C520`) this is the Cheat Engine address
`0x6E7CA30`; it matched on every logged frame.

## Frame order

Within one frame (about 11 ms at 90 Hz):

1. The camera matrix is updated.
2. The HUD builder runs for the menu panel, then the main HUD (worker thread).
3. `IVRCompositor::WaitGetPoses` returns the next poses (render thread).
4. `0x2C356C0` runs on the main thread. It pumps `IVRSystem` events (vtable `+0xF0`, 64-byte events)
   and does not fetch poses.

So a HUD matrix replaced inside the builder uses the camera of the frame being rendered.

The game gets `IVRCompositor_029` through `VR_GetGenericInterface` and caches it in the OpenVR context
at `0x70F45E8`.

## Head-lock formula

With `A` = anchor local, `C` = camera, `O` = the game's local matrix for the HUD, and `N` = the
anchor's orientation (row lengths normalised) placed at the camera position:

    local = O * A * inverse(N) * C * inverse(A)

`O * A * inverse(N)` is the HUD as seen from the eyes when looking straight ahead; multiplying by `C`
makes it follow the head. Looking straight ahead the result equals `O`. The game's column-3 values
of `O` are copied into the result.

The mod falls back to the game's matrix when:
- there is no valid camera;
- the anchor is not on a pure-translation origin node;
- the anchor is more than 50 m from the camera;
- the HUD would be more than 10 m from the eyes, which happens for a fraction of a second during ship
  enter/exit animations.

## Earlier leads

- The "HMD quaternion" at `0x6DFECC8`/`CD0`/`CD8`/`CE0` reads components 8 bytes apart, and the
  logged values have length ≈ 0.11 rather than 1. It is probably not a quaternion.
- `0x337927` (CameraTransformLogger's hook point) copies 80 bytes of camera state to `0x6EB4B00`
  every frame.

## FrameProbe

`tools/FrameProbe` writes `FrameProbe_<time>.csv` next to the `.asi`. Row types:
- `SNAP`: HUD node, anchor, camera and HMD pose, once per frame per HUD node.
- `AM`: each HUD `ApplyMatrix` call, with the lock result.
- `HB`: HUD builder calls.
- `POSE`: `WaitGetPoses`.
- `CHAIN`/`NODE`: ancestor dumps.
- `MODE`/`MARK`: key presses.

The header lines describe the columns, and `tools/analysis/probe_log.py` loads a log into pandas
DataFrames.
