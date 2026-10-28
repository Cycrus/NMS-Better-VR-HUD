# NMS update: static hook compatibility analysis

Analyzed 2026-10-28 with radare2, without executing either game binary.

## Confirmed immediate failure

The original `VR_UPDATE_SIGNATURE` does not occur in `NMS_new.exe`.
The other three signatures each still occur exactly once in executable code.
`ResolveHookTargets()` requires all four resolutions before installing any
hooks, so this mismatch causes the mod to install **no hooks**. The failure
only goes to `OutputDebugStringA`, explaining the reported silent no-op.

The failed signature contains a structure-member displacement that changed:

```asm
; NMS_old.exe
0x142c3d31c  80b98416000000  cmp byte [rcx + 0x1684], 0
0x142c3d323  7505            jne 0x142c3d32a
0x142c3d325  e8a63e0000      call 0x142c411d0

; NMS_new.exe
0x142caca2c  80b99416000000  cmp byte [rcx + 0x1694], 0
0x142caca33  7505            jne 0x142caca3a
0x142caca35  e8e6400000      call 0x142cb0b20
```

Replacing `80 B9 84 16 00 00 00` with `80 B9 ?? 16 00 00 00` in the
otherwise unchanged signature matches exactly once in **both supplied
binaries**. The `call rel32` offset remains `0x23`. This is the smallest
compatibility fix. Exact new-only `94` also matches, but drops old support.

## Inputs and address conventions

All VAs below use preferred image base `0x140000000`; ASLR requires resolving
from the runtime module base. File SHA-256:

```text
NMS_old.exe  13d5060d4efb9d2a6a6b1b349bc4257231056cc2a055df4bb15d816262cc3499
NMS_new.exe  d1c7155764271eff0733c37d1849e685a11da3d54a41d685d7898fc0aad83a87
```

The supplied `NMS_old.exe` is not the exact original binary documented in
`signatures.md`: its RVAs already differ slightly from that document. Scanning
and comparing the actual supplied files avoids relying on historical RVAs.
Also, `.text` begins at file offset `0x400` in old versus `0x600` in new,
while its RVA is `0x1000` in both. File offsets must be translated through
the PE section table, rather than using one conversion constant for both.

## Match counts and relocated targets

| Resolver | Original signature: old | Original signature: new | Old target RVA | New target RVA | Movement |
|---|---:|---:|---|---|---|
| Apply matrix | 1 | 1 | `0x18452D0` | `0x187C2C0` | `+0x36FF0` |
| Body capture | 1 | 1 | `0x3377AD` | `0x33E9AD` | `+0x7200` |
| Camera capture | 1 | 1 | `0x337917` | `0x33EB17` | `+0x7200` |
| VR update | 1 | **0** | `0x2C411D0` | `0x2CB0B20` after fix | `+0x6F950` |

New match VAs, before resolver adjustment:

```text
APPLY_MATRIX_SIGNATURE          0x140AD38C8  call at +0x05
BODY_CAPTURE_POINT_SIGNATURE    0x14033E975  hook at +0x38
CAMERA_CAPTURE_POINT_SIGNATURE  0x14033EAB2  hook at +0x65
VR_UPDATE_SIGNATURE (fixed)     0x142CACA12  call at +0x23
```

Movement alone does not break these resolvers: their job is to recover moved
addresses. The literal structure offset in the VR signature is what broke.

## Hook semantics remain compatible

### Body capture

New code still writes the same transform rows through `r15` immediately
before the hook. The offset, register, and instruction boundary remain valid:

```asm
0x14033e975  movups xmmword [r15 + 0x5b0], xmm0
0x14033e981  movups xmmword [r15 + 0x5c0], xmm1
0x14033e98d  movups xmmword [r15 + 0x5d0], xmm0
0x14033e999  movups xmmword [r15 + 0x5e0], xmm1
0x14033e9a5  movups xmmword [r15 + 0x5f0], xmm0
; hook here, after writes:
0x14033e9ad  mov rcx, qword [0x146f60828]
0x14033e9b4  add rcx, 0x92cc50
0x14033e9bb  mov rax, qword [rcx]
0x14033e9be  call qword [rax + 0x70]
```

`CapturePlayerBodyMatrix(r15)` continues to read 16 floats at `r15+0x5B0`.
The fifth 16-byte write exists in the game but is not part of the 4x4 matrix
copied by the plugin; this is unchanged.

### Camera capture

New code retains the copy into `r15+0x510` and the capture point following it:

```asm
0x14033eab2  movups xmm0, xmmword [r15 + 0x560]
0x14033eaba  movups xmm1, xmmword [r15 + 0x570]
0x14033eac2  movups xmmword [r15 + 0x510], xmm0
0x14033eaca  movups xmm0, xmmword [r15 + 0x580]
0x14033ead2  movups xmmword [r15 + 0x520], xmm1
0x14033eada  movups xmm1, xmmword [r15 + 0x590]
0x14033eae2  movups xmmword [r15 + 0x530], xmm0
0x14033eaea  movups xmm0, xmmword [r15 + 0x5a0]
0x14033eaf2  movups xmmword [r15 + 0x540], xmm1
0x14033eafa  movups xmmword [r15 + 0x550], xmm0
0x14033eb02  mov rax, qword [0x146f60828]
0x14033eb09  mov byte [rax + 0x930c09], 0
0x14033eb10  mov rcx, qword [0x146f60828]
; hook here:
0x14033eb17  movups xmm0, xmmword [r15 + 0x5b0]
```

The conditional branch immediately above this copy can jump straight to
`0x14033EB17`. Thus the hook also executes when the special copy is skipped,
reading the current camera matrix already maintained elsewhere in the same
routine. This control-flow pattern is present in the old version as well.
The existing naked hooks preserve flags, general registers and XMM registers,
allocate Windows shadow space, and preserve the surrounding in-function stack
alignment (their total stack subtraction is a multiple of 16). No new register
or stack adjustment is indicated by these relocated sites. This is a static
compatibility observation, not proof of absence of runtime races.

### Apply matrix

The resolver still reaches the function entry, with `ecx` holding a 32-bit
object handle and `rdx` a matrix pointer:

```asm
0x140ad38c8  lea rdx, [rsp + 0x20]
0x140ad38cd  call 0x14187c2c0
0x140ad38d2  movaps xmm13, xmmword [rsp + 0x60]

0x14187c2c0  mov r9d, ecx
0x14187c2c3  mov r10, rdx
0x14187c2c6  shr r9d, 0x13
0x14187c2ca  test r9d, r9d
0x14187c2cd  je 0x14187c329
0x14187c2cf  mov r8d, ecx
0x14187c2d2  and r8d, 0x7ffff
; look up object, compare stored index and generation with incoming handle
0x14187c319  shr edx, 0x13
0x14187c31c  cmp edx, r9d
0x14187c31f  jne 0x14187c329
0x14187c321  mov rdx, r10
0x14187c324  jmp 0x141916150
0x14187c329  ret
```

The old implementation at `0x1418452D0` has the same handle-validation
shape. This remains an appropriate global object-transform interception point.

### VR update

The recovered new target has the same entry shape as old and remains a large
VR pose/frame update routine. Both save `self` from `rcx` to `rsi`, check the
current thread, obtain tracked transforms through virtual calls, build pose
matrices and set the completion flag tested by the signature's caller:

```asm
; old
0x142c411d0  push rbp
0x142c411d2  push rbx
0x142c411d3  push rsi
0x142c411d4  lea rbp, [rsp - 0xb0]
0x142c411dc  sub rsp, 0x1b0
0x142c411e3  mov rsi, rcx
0x142c4125b  mov rcx, qword [rsi + 0x1688]
0x142c4127f  call qword [rax + 0xf0]
0x142c41717  mov byte [rsi + 0x1684], 1

; new
0x142cb0b20  push rbp
0x142cb0b22  push rbx
0x142cb0b23  push rsi
0x142cb0b24  lea rbp, [rsp - 0xb0]
0x142cb0b2c  sub rsp, 0x1b0
0x142cb0b33  mov rsi, rcx
0x142cb0bab  mov rcx, qword [rsi + 0x1698]
0x142cb0bcf  call qword [rax + 0xf0]
0x142cb1067  mov byte [rsi + 0x1694], 1
```

Several corresponding members shifted by `+0x10` or `+0x20`, supporting a
structure-layout change rather than a different function being selected.
The plugin treats `self` as opaque, so these field changes require no changes
to its `HookVrUpdate(void*)` wrapper.

There is no direct `call applyMatrix` in the recovered update routine in
either supplied binary. The historical documentation should not be taken as
a verified direct call graph. The mod's relevant path is its own explicit
`HookVrUpdate -> originalVrUpdate -> ApplyCameraRelativeHudMatrix ->
originalApplyMatrix`; that path remains structurally viable after resolution.

## Runtime questions not resolved by static analysis

1. **HUD object handle.** `0x00080133` is an empirical runtime handle, not a
   stable symbolic identifier. The signature caller's containing function
   starts at new VA `0x140AD3650`, and loads handles from `[self+0x1C]` and
   `[self+0x20]`; the value is not established as a literal in the assembly.
   Apply-matrix verifies both handle index and generation, and silently returns
   for invalid handles. Static analysis cannot establish that `0x80133` still
   names the HUD in a live session. If patched builds still do nothing, log
   actual incoming handle values and correlate HUD updates before changing it.
2. **Execution frequency and ordering.** The selected VR update is still the
   corresponding pose path, but actual per-frame execution and ordering versus
   capture callbacks require in-game counters/logging.
3. **Captured matrices and stutter.** Matrix offsets/registers remain intact,
   but concurrent copies are not synchronized as a whole. Existing comments
   already acknowledge possible races/stutter; this is separate from the
   confirmed pre-install signature failure.
4. **Loader.** Static examination does not verify that the ASI loader loaded
   the file. A visible startup log can distinguish loader failure, resolution
   failure, hook-install failure, missing capture calls and missing HUD handles.

Recommended order: test the minimal signature fix first. If visible HUD tracking
returns, the immediate regression is resolved. Otherwise use an instrumented
plugin to record startup, all resolved RVAs, hook return statuses, update/capture
counts, matrix validity, and observed target-handle hits before selecting any
further memory offsets or object handles.

## Patch validation

The minimal wildcard change is applied in `src/nms_signatures.cpp`.
Both `BetterVrHudFull.asi` and `BetterVrHudLevel.asi` build successfully with
`make -C src -j2`. Existing host math and signature-parser tests also pass.
All four patched patterns match uniquely in both supplied executables; the
resolved addresses were independently checked against their PE section tables.
In-game verification is pending.

## Reproduction commands

Useful targeted r2 commands (no full-program analysis required):

```sh
r2 -q -e scr.color=0 -c 'iS; iI' ../NMS_new.exe
r2 -q -e scr.color=0 -c 'pd 22 @ 0x142caca12; pd 55 @ 0x14033e975; pd 45 @ 0x14033eab2' ../NMS_new.exe
r2 -q -e scr.color=0 -c 'pd 65 @ 0x14187c2c0; pd 130 @ 0x142cb0b20' ../NMS_new.exe
r2 -q -e scr.color=0 -c 'pd 15 @ 0x142c3d302; pd 50 @ 0x142c411d0' ../NMS_old.exe
```

Signature counts were checked by parsing the source's byte patterns, matching
fixed bytes and wildcard bytes against the executable's contents, and mapping
hits through the PE sections. Hits and resolved call targets were then checked
in r2 disassembly. No binary patches or game execution were performed.
