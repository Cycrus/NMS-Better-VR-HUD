# Runtime Signatures

This document records the signatures used to replace static NMS.exe code
offsets in `src/better_vr_hud.cpp`.

The signatures were derived from:

```text
diagnostics/game_structure_data/reverse/NMS.exe
```

The executable image base in the analyzed binary is `0x140000000`. The
existing offsets are RVAs relative to that base. Pattern scans should run
against executable sections only, and each pattern must resolve to exactly one
match before any hook is installed.

For signatures that resolve through a `call rel32`, read the signed 32-bit
relative displacement after the `E8` opcode:

```cpp
uint8_t* call = match + callOffset;
int32_t rel = *reinterpret_cast<int32_t*>(call + 1);
uintptr_t target = reinterpret_cast<uintptr_t>(call + 5 + rel);
```

If any signature has zero or multiple matches, fail closed and skip hook
installation.

## `APPLY_MATRIX_OFFSET`

Current RVA:

```text
0x1838500
```

Current VA in the analyzed binary:

```text
0x141838500
```

Recommended signature:

```text
48 8D 54 24 20 E8 ?? ?? ?? ?? 44 0F 28 6C 24 60 4C 8D 9C 24 E8 00 00 00
```

Resolution:

```text
match VA        = 0x140AB1C98
call opcode VA  = match + 0x05 = 0x140AB1C9D
resolved target = call + 5 + rel32 = 0x141838500
```

Relevant disassembly:

```asm
0x140AB1C98  lea rdx, [rsp + 0x20]
0x140AB1C9D  call 0x141838500
0x140AB1CA2  movaps xmm13, xmmword [rsp + 0x60]
```

Use this HUD-specific callsite to resolve the function target, then keep the
existing MinHook behavior of hooking the resolved function address. Do not
switch to patching only this callsite unless the intended behavior changes from
global `applyMatrix` interception to this single HUD-specific caller.

Fallback function-body signature:

```text
44 8B C9 4C 8B D2 41 C1 E9 13 45 85 C9 74 5A 44 8B C1 41 81 E0 FF FF 07 00 41 81 F8 FF FF 07 00 74 47 48 8B 15 ?? ?? ?? ?? 81 E1 FF FF 07 00 48 8B 82 E8 00 00 00
```

The fallback matched uniquely at `0x141838500` in the analyzed binary, but the
function has many callers. Prefer the callsite signature above because it is
tied to the HUD transform path.

## `BODY_CAPTURE_POINT_OFFSET`

Current RVA:

```text
0x3377BD
```

Current VA in the analyzed binary:

```text
0x1403377BD
```

Recommended signature:

```text
41 0F 11 87 B0 05 00 00
0F 10 48 10
41 0F 11 8F C0 05 00 00
0F 10 40 20
41 0F 11 87 D0 05 00 00
0F 10 48 30
41 0F 11 8F E0 05 00 00
0F 10 40 40
41 0F 11 87 F0 05 00 00
48 8B 0D ?? ?? ?? ??
48 81 C1 ?? ?? ?? ??
48 8B 01
FF 50 70
```

Resolution:

```text
match VA = 0x140337785
hook VA  = match + 0x38 = 0x1403377BD
hook RVA = 0x3377BD
```

Relevant disassembly at the hook point:

```asm
0x1403377B5  movups xmmword [r15 + 0x5f0], xmm0
0x1403377BD  mov rcx, qword [rip + ...]
0x1403377C4  add rcx, 0x8f5a90
0x1403377CB  mov rax, qword [rcx]
0x1403377CE  call qword [rax + 0x70]
```

This is an in-function capture point. The hook address is an instruction
boundary, and the current naked hook relies on `r15` still pointing at the
structure containing the body transform. The signature intentionally keeps the
nearby `r15 + 0x5B0..0x5F0` accesses as semantic anchors and wildcards
RIP-relative displacements and the `add rcx, imm32` immediate.

## `CAMERA_CAPTURE_POINT_OFFSET`

Current RVA:

```text
0x337927
```

Current VA in the analyzed binary:

```text
0x140337927
```

Recommended signature:

```text
41 0F 10 87 60 05 00 00
41 0F 10 8F 70 05 00 00
41 0F 11 87 10 05 00 00
41 0F 10 87 80 05 00 00
41 0F 11 8F 20 05 00 00
41 0F 10 8F 90 05 00 00
41 0F 11 87 30 05 00 00
41 0F 10 87 A0 05 00 00
41 0F 11 8F 40 05 00 00
41 0F 11 87 50 05 00 00
48 8B 05 ?? ?? ?? ??
C6 80 ?? ?? ?? ?? ??
48 8B 0D ?? ?? ?? ??
41 0F 10 87 B0 05 00 00
48 81 C1 ?? ?? ?? ??
0F 29 05 ?? ?? ?? ??
41 0F 10 8F C0 05 00 00
0F 29 0D ?? ?? ?? ??
41 0F 10 87 D0 05 00 00
0F 29 05 ?? ?? ?? ??
```

Resolution:

```text
match VA = 0x1403378C2
hook VA  = match + 0x65 = 0x140337927
hook RVA = 0x337927
```

Relevant disassembly around the hook point:

```asm
0x1403378D2  movups xmmword [r15 + 0x510], xmm0
0x1403378E2  movups xmmword [r15 + 0x520], xmm1
0x1403378F2  movups xmmword [r15 + 0x530], xmm0
0x140337902  movups xmmword [r15 + 0x540], xmm1
0x14033790A  movups xmmword [r15 + 0x550], xmm0
0x140337920  mov rcx, qword [rip + ...]
0x140337927  movups xmm0, xmmword [r15 + 0x5b0]
```

This hook point is an instruction boundary. The camera matrix rows at
`r15 + 0x510..0x550` have just been written when execution reaches the hook.
The instruction at the hook address begins the following `r15 + 0x5B0` block,
so comments and resolver names should describe the hook as "after camera matrix
write" rather than as the instruction that reads the camera matrix.

## `VR_UPDATE_OFFSET`

Current RVA:

```text
0x2C356C0
```

Current VA in the analyzed binary:

```text
0x142C356C0
```

Recommended signature:

```text
80 3D ?? ?? ?? ?? 00
0F 85 ?? ?? ?? ??
80 3D ?? ?? ?? ?? 00
0F 85 ?? ?? ?? ??
80 B9 84 16 00 00 00
75 05
E8 ?? ?? ?? ??
48 8B 0D ?? ?? ?? ??
48 8B 01
FF 10
84 C0
```

Resolution:

```text
match VA        = 0x142C31802
call opcode VA  = match + 0x23 = 0x142C31825
resolved target = call + 5 + rel32 = 0x142C356C0
```

Relevant disassembly:

```asm
0x142C3181C  cmp byte [rcx + 0x1684], 0
0x142C31823  jne 0x142C3182A
0x142C31825  call 0x142C356C0
0x142C3182A  mov rcx, qword [rip + ...]
0x142C31831  mov rax, qword [rcx]
0x142C31834  call qword [rax]
0x142C31836  test al, al
```

Use the callsite to resolve the function address, then keep the existing
MinHook behavior of hooking the resolved function. A function-body fallback was
also unique in the analyzed binary, but it includes prologue and early
state-check logic and may be more sensitive to compiler changes.
