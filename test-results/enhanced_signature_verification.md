# Enhanced signature uniqueness verification

Verified 2026-10-28 against `../NMS_new.exe`, SHA-256
`d1c7155764271eff0733c37d1849e685a11da3d54a41d685d7898fc0aad83a87`.
The executable was read as data and never executed. After verification, both
enhancements were applied to `src/nms_signatures.cpp`. Both ASI variants build
successfully, and `make -C src test` passes. In-game verification of these
broader patterns is pending; the initial narrower fix was confirmed working.

## Result

**Both enhanced signatures remain unique.** Each matches once independently.
Using both enhancements together, with unchanged body/camera patterns, leaves
all four patterns unique and resolves the same four hook targets as before.

Scan scope: all PE sections marked `IMAGE_SCN_MEM_EXECUTE` (`.text`, `.text.un`,
`.bind`). File-backed section bytes were searched with byte wildcards and an
overlapping-match regex lookahead; every hit is in `.text`. Virtual zero padding
cannot add matches because every pattern starts with a fixed nonzero byte.
Addresses use preferred image base `0x140000000`; ASLR requires runtime rebasing.

| Pattern | Matches | Match VA | Match RVA | Resolution | Target VA | Target RVA |
|---|---:|---|---|---|---|---|
| APPLY_MATRIX | 1 | `0x140ad38c8` | `0xad38c8` | call +`0x5` | `0x14187c2c0` | `0x187c2c0` |
| BODY_CAPTURE_POINT | 1 | `0x14033e975` | `0x33e975` | hook +`0x38` | `0x14033e9ad` | `0x33e9ad` |
| CAMERA_CAPTURE_POINT | 1 | `0x14033eab2` | `0x33eab2` | hook +`0x65` | `0x14033eb17` | `0x33eb17` |
| VR_UPDATE | 1 | `0x142caca12` | `0x2caca12` | call +`0x23` | `0x142cb0b20` | `0x2cb0b20` |

## Exact patterns tested

### APPLY_MATRIX

```text
48 8D 54 24 ?? E8 ?? ?? ?? ?? 44 0F 28 6C 24 ?? 4C 8D 9C 24 ?? ?? ?? ??
```

### BODY_CAPTURE_POINT

```text
41 0F 11 87 B0 05 00 00 0F 10 48 10 41 0F 11 8F C0 05 00 00 0F 10 40 20 41 0F 11 87 D0 05 00 00 0F 10 48 30 41 0F 11 8F E0 05 00 00 0F 10 40 40 41 0F 11 87 F0 05 00 00 48 8B 0D ?? ?? ?? ?? 48 81 C1 ?? ?? ?? ?? 48 8B 01 FF 50 70
```

### CAMERA_CAPTURE_POINT

```text
41 0F 10 87 60 05 00 00 41 0F 10 8F 70 05 00 00 41 0F 11 87 10 05 00 00 41 0F 10 87 80 05 00 00 41 0F 11 8F 20 05 00 00 41 0F 10 8F 90 05 00 00 41 0F 11 87 30 05 00 00 41 0F 10 87 A0 05 00 00 41 0F 11 8F 40 05 00 00 41 0F 11 87 50 05 00 00 48 8B 05 ?? ?? ?? ?? C6 80 ?? ?? ?? ?? ?? 48 8B 0D ?? ?? ?? ?? 41 0F 10 87 B0 05 00 00 48 81 C1 ?? ?? ?? ?? 0F 29 05 ?? ?? ?? ?? 41 0F 10 8F C0 05 00 00 0F 29 0D ?? ?? ?? ?? 41 0F 10 87 D0 05 00 00 0F 29 05 ?? ?? ?? ??
```

### VR_UPDATE

```text
80 3D ?? ?? ?? ?? 00 0F 85 ?? ?? ?? ?? 80 3D ?? ?? ?? ?? 00 0F 85 ?? ?? ?? ?? 80 B9 ?? ?? ?? ?? 00 75 05 E8 ?? ?? ?? ?? 48 8B 0D ?? ?? ?? ?? 48 8B 01 FF 10 84 C0
```

## radare2 cross-check

The call at apply-matrix match `+0x05` is an `E8 rel32` instruction on an
instruction boundary. It resolves to `0x14187C2C0`; the function still consumes
the object handle in `ecx` and matrix pointer in `rdx`:

```asm
0x140ad38c8  lea rdx, [rsp + 0x20]
0x140ad38cd  call 0x14187c2c0
0x140ad38d2  movaps xmm13, xmmword [rsp + 0x60]
0x140ad38d8  lea r11, [rsp + 0xe8]

0x14187c2c0  mov r9d, ecx
0x14187c2c3  mov r10, rdx
0x14187c2c6  shr r9d, 0x13
```

The VR call at match `+0x23` is also an `E8 rel32` instruction on an instruction
boundary. It resolves to `0x142CB0B20`, whose prologue preserves the `self`
pointer from `rcx` in `rsi`, consistent with the current wrapper:

```asm
0x142caca2c  cmp byte [rcx + 0x1694], 0
0x142caca33  jne 0x142caca3a
0x142caca35  call 0x142cb0b20
0x142caca3a  mov rcx, qword [0x145c776a8]

0x142cb0b20  push rbp
0x142cb0b22  push rbx
0x142cb0b23  push rsi
0x142cb0b24  lea rbp, [rsp - 0xb0]
0x142cb0b2c  sub rsp, 0x1b0
0x142cb0b33  mov rsi, rcx
```

The unchanged body/camera offsets also land on the expected instruction starts:

```asm
0x14033e9ad  mov rcx, qword [0x146f60828]
0x14033eb17  movups xmm0, xmmword [r15 + 0x5b0]
```

Useful reproduction command:

```sh
r2 -q -e scr.color=0 -c 'pd 6 @ 0x140ad38c8; pd 10 @ 0x14187c2c0; pd 12 @ 0x142caca12; pd 9 @ 0x142cb0b20; pd 3 @ 0x14033e9ad; pd 3 @ 0x14033eb17; q' ../NMS_new.exe
```

This confirms uniqueness and target continuity in this executable. It does not
guarantee matching or semantic validity after an arbitrary future compiler or
game-code change. The proposed wildcarding does not change instruction lengths
or resolver offsets in the current binary.
