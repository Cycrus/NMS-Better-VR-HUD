#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "MinHook.h"
#include "better_vr_hud_math.h"

static constexpr uintptr_t BODY_MATRIX_R15_OFFSET = 0x5B0;
static constexpr uintptr_t CAMERA_MATRIX_R15_OFFSET = 0x510;
static constexpr uint32_t TARGET_HUD_HANDLE = 0x00080133;
static constexpr float HUD_OFFSET_X = 0.0f;
static constexpr float HUD_OFFSET_Y = 0.0f;
static constexpr float DEFAULT_HUD_OFFSET_Z = -2.5f;
static constexpr size_t MAX_PATTERN_BYTES = 256;
static constexpr size_t MAX_EXECUTABLE_SECTIONS = 32;

static constexpr const char* APPLY_MATRIX_SIGNATURE =
    "48 8D 54 24 20 E8 ?? ?? ?? ?? 44 0F 28 6C 24 60 "
    "4C 8D 9C 24 E8 00 00 00";

static constexpr const char* BODY_CAPTURE_POINT_SIGNATURE =
    "41 0F 11 87 B0 05 00 00 "
    "0F 10 48 10 "
    "41 0F 11 8F C0 05 00 00 "
    "0F 10 40 20 "
    "41 0F 11 87 D0 05 00 00 "
    "0F 10 48 30 "
    "41 0F 11 8F E0 05 00 00 "
    "0F 10 40 40 "
    "41 0F 11 87 F0 05 00 00 "
    "48 8B 0D ?? ?? ?? ?? "
    "48 81 C1 ?? ?? ?? ?? "
    "48 8B 01 "
    "FF 50 70";

static constexpr const char* CAMERA_CAPTURE_POINT_SIGNATURE =
    "41 0F 10 87 60 05 00 00 "
    "41 0F 10 8F 70 05 00 00 "
    "41 0F 11 87 10 05 00 00 "
    "41 0F 10 87 80 05 00 00 "
    "41 0F 11 8F 20 05 00 00 "
    "41 0F 10 8F 90 05 00 00 "
    "41 0F 11 87 30 05 00 00 "
    "41 0F 10 87 A0 05 00 00 "
    "41 0F 11 8F 40 05 00 00 "
    "41 0F 11 87 50 05 00 00 "
    "48 8B 05 ?? ?? ?? ?? "
    "C6 80 ?? ?? ?? ?? ?? "
    "48 8B 0D ?? ?? ?? ?? "
    "41 0F 10 87 B0 05 00 00 "
    "48 81 C1 ?? ?? ?? ?? "
    "0F 29 05 ?? ?? ?? ?? "
    "41 0F 10 8F C0 05 00 00 "
    "0F 29 0D ?? ?? ?? ?? "
    "41 0F 10 87 D0 05 00 00 "
    "0F 29 05 ?? ?? ?? ??";

static constexpr const char* VR_UPDATE_SIGNATURE =
    "80 3D ?? ?? ?? ?? 00 "
    "0F 85 ?? ?? ?? ?? "
    "80 3D ?? ?? ?? ?? 00 "
    "0F 85 ?? ?? ?? ?? "
    "80 B9 84 16 00 00 00 "
    "75 05 "
    "E8 ?? ?? ?? ?? "
    "48 8B 0D ?? ?? ?? ?? "
    "48 8B 01 "
    "FF 10 "
    "84 C0";

static constexpr size_t APPLY_MATRIX_CALL_OFFSET = 0x05;
static constexpr size_t BODY_CAPTURE_POINT_HOOK_OFFSET = 0x38;
static constexpr size_t CAMERA_CAPTURE_POINT_HOOK_OFFSET = 0x65;
static constexpr size_t VR_UPDATE_CALL_OFFSET = 0x23;

using ApplyMatrixFn = void (WINAPI*)(uint32_t handle, float* matrix);
using VrUpdateFn = void (WINAPI*)(void* self);

struct PatternByte
{
    uint8_t value;
    bool wildcard;
};

struct CompiledPattern
{
    PatternByte bytes[MAX_PATTERN_BYTES];
    size_t length;
};

struct ExecutableSection
{
    uintptr_t start;
    uintptr_t end;
};

struct ModuleExecutableSections
{
    uint8_t* base;
    ExecutableSection sections[MAX_EXECUTABLE_SECTIONS];
    size_t count;
};

struct HookTargets
{
    void* applyMatrix;
    void* bodyCapturePoint;
    void* cameraCapturePoint;
    void* vrUpdate;
};

extern "C" void* g_originalBodyCapturePoint;
extern "C" void* g_originalCameraCapturePoint;

void* g_originalBodyCapturePoint = nullptr;
void* g_originalCameraCapturePoint = nullptr;

static ApplyMatrixFn g_originalApplyMatrix = nullptr;
static VrUpdateFn g_originalVrUpdate = nullptr;

static float g_latestBodyMatrix[16] = {};
static float g_latestCameraMatrix[16] = {};
static HudMatrixState g_hudMatrixState = {};
static float g_latestHudOffsetZ = DEFAULT_HUD_OFFSET_Z;
static volatile LONG g_hasBodyMatrix = 0;
static volatile LONG g_hasCameraMatrix = 0;
static volatile LONG g_hasHudOffsetZ = 0;

static void DebugLog(const char* message)
{
    OutputDebugStringA("BetterVrHud: ");
    OutputDebugStringA(message);
    OutputDebugStringA("\r\n");
}

static void DebugLogFormat(const char* format, ...)
{
    char buffer[512] = {};
    va_list args;
    va_start(args, format);
    std::vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);

    DebugLog(buffer);
}

static bool IsPatternSpace(char c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

static int HexValue(char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;

    return -1;
}

static bool CompilePattern(const char* text, CompiledPattern* pattern)
{
    pattern->length = 0;

    while (*text)
    {
        while (IsPatternSpace(*text))
            ++text;

        if (!*text)
            break;

        if (pattern->length >= MAX_PATTERN_BYTES)
        {
            DebugLog("signature is longer than the local pattern buffer");
            return false;
        }

        if (*text == '?')
        {
            ++text;
            if (*text == '?')
                ++text;

            pattern->bytes[pattern->length++] = {0, true};
            continue;
        }

        int high = HexValue(text[0]);
        int low = HexValue(text[1]);
        if (high < 0 || low < 0)
        {
            DebugLog("signature contains an invalid byte token");
            return false;
        }

        pattern->bytes[pattern->length++] = {
            static_cast<uint8_t>((high << 4) | low),
            false
        };
        text += 2;
    }

    if (pattern->length == 0)
    {
        DebugLog("signature is empty");
        return false;
    }

    return true;
}

static bool GetMainModuleExecutableSections(ModuleExecutableSections* module)
{
    module->base = reinterpret_cast<uint8_t*>(GetModuleHandleA(nullptr));
    module->count = 0;

    if (!module->base)
    {
        DebugLog("GetModuleHandleA(nullptr) failed");
        return false;
    }

    auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(module->base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE)
    {
        DebugLog("main module has an invalid DOS header");
        return false;
    }

    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(module->base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE)
    {
        DebugLog("main module has an invalid NT header");
        return false;
    }

    IMAGE_SECTION_HEADER* sections = IMAGE_FIRST_SECTION(nt);
    for (WORD i = 0; i < nt->FileHeader.NumberOfSections; ++i)
    {
        const IMAGE_SECTION_HEADER& section = sections[i];
        if (!(section.Characteristics & IMAGE_SCN_MEM_EXECUTE))
            continue;

        DWORD sectionSize = section.Misc.VirtualSize;
        if (sectionSize == 0)
            sectionSize = section.SizeOfRawData;
        if (sectionSize == 0)
            continue;

        if (module->count >= MAX_EXECUTABLE_SECTIONS)
        {
            DebugLog("main module has more executable sections than expected");
            return false;
        }

        uintptr_t start = reinterpret_cast<uintptr_t>(module->base) + section.VirtualAddress;
        uintptr_t end = start + sectionSize;
        if (end <= start)
        {
            DebugLog("main module executable section range overflowed");
            return false;
        }

        module->sections[module->count++] = {start, end};
    }

    if (module->count == 0)
    {
        DebugLog("main module has no executable sections");
        return false;
    }

    return true;
}

static bool IsExecutableRange(
    const ModuleExecutableSections& module,
    uintptr_t address,
    size_t size
)
{
    if (size == 0)
        return false;

    for (size_t i = 0; i < module.count; ++i)
    {
        const ExecutableSection& section = module.sections[i];
        if (address >= section.start && address < section.end && size <= section.end - address)
            return true;
    }

    return false;
}

static bool PatternMatches(const uint8_t* cursor, const CompiledPattern& pattern)
{
    for (size_t i = 0; i < pattern.length; ++i)
    {
        if (!pattern.bytes[i].wildcard && cursor[i] != pattern.bytes[i].value)
            return false;
    }

    return true;
}

static bool FindUniquePattern(
    const ModuleExecutableSections& module,
    const char* name,
    const char* signature,
    uintptr_t* match
)
{
    CompiledPattern pattern = {};
    if (!CompilePattern(signature, &pattern))
    {
        DebugLogFormat("%s signature failed to compile", name);
        return false;
    }

    uintptr_t found = 0;
    size_t matchCount = 0;

    for (size_t i = 0; i < module.count; ++i)
    {
        const ExecutableSection& section = module.sections[i];
        size_t sectionSize = section.end - section.start;
        if (sectionSize < pattern.length)
            continue;

        const uint8_t* begin = reinterpret_cast<const uint8_t*>(section.start);
        size_t lastOffset = sectionSize - pattern.length;
        for (size_t offset = 0; offset <= lastOffset; ++offset)
        {
            if (!PatternMatches(begin + offset, pattern))
                continue;

            found = section.start + offset;
            ++matchCount;
            if (matchCount > 1)
            {
                DebugLogFormat("%s signature matched more than once", name);
                return false;
            }
        }
    }

    if (matchCount != 1)
    {
        DebugLogFormat("%s signature did not match", name);
        return false;
    }

    *match = found;
    return true;
}

static bool ResolveCallRel32Target(
    const ModuleExecutableSections& module,
    const char* name,
    const char* signature,
    size_t callOffset,
    uintptr_t* target
)
{
    uintptr_t match = 0;
    if (!FindUniquePattern(module, name, signature, &match))
        return false;

    uintptr_t callAddress = match + callOffset;
    if (!IsExecutableRange(module, callAddress, 5))
    {
        DebugLogFormat("%s callsite is outside executable sections", name);
        return false;
    }

    auto* call = reinterpret_cast<const uint8_t*>(callAddress);
    if (call[0] != 0xE8)
    {
        DebugLogFormat("%s callsite does not start with call rel32", name);
        return false;
    }

    int32_t rel = 0;
    std::memcpy(&rel, call + 1, sizeof(rel));

    uintptr_t resolved = static_cast<uintptr_t>(
        static_cast<intptr_t>(callAddress + 5) + static_cast<intptr_t>(rel)
    );
    if (!IsExecutableRange(module, resolved, 1))
    {
        DebugLogFormat("%s resolved target is outside executable sections", name);
        return false;
    }

    *target = resolved;
    return true;
}

static bool ResolvePatternOffsetTarget(
    const ModuleExecutableSections& module,
    const char* name,
    const char* signature,
    size_t hookOffset,
    uintptr_t* target
)
{
    uintptr_t match = 0;
    if (!FindUniquePattern(module, name, signature, &match))
        return false;

    uintptr_t resolved = match + hookOffset;
    if (!IsExecutableRange(module, resolved, 1))
    {
        DebugLogFormat("%s hook target is outside executable sections", name);
        return false;
    }

    *target = resolved;
    return true;
}

static bool ResolveHookTargets(HookTargets* targets)
{
    ModuleExecutableSections module = {};
    if (!GetMainModuleExecutableSections(&module))
        return false;

    uintptr_t applyMatrix = 0;
    uintptr_t bodyCapturePoint = 0;
    uintptr_t cameraCapturePoint = 0;
    uintptr_t vrUpdate = 0;

    if (!ResolveCallRel32Target(
            module,
            "APPLY_MATRIX",
            APPLY_MATRIX_SIGNATURE,
            APPLY_MATRIX_CALL_OFFSET,
            &applyMatrix
        ))
        return false;

    if (!ResolvePatternOffsetTarget(
            module,
            "BODY_CAPTURE_POINT",
            BODY_CAPTURE_POINT_SIGNATURE,
            BODY_CAPTURE_POINT_HOOK_OFFSET,
            &bodyCapturePoint
        ))
        return false;

    if (!ResolvePatternOffsetTarget(
            module,
            "CAMERA_CAPTURE_POINT",
            CAMERA_CAPTURE_POINT_SIGNATURE,
            CAMERA_CAPTURE_POINT_HOOK_OFFSET,
            &cameraCapturePoint
        ))
        return false;

    if (!ResolveCallRel32Target(
            module,
            "VR_UPDATE",
            VR_UPDATE_SIGNATURE,
            VR_UPDATE_CALL_OFFSET,
            &vrUpdate
        ))
        return false;

    targets->applyMatrix = reinterpret_cast<void*>(applyMatrix);
    targets->bodyCapturePoint = reinterpret_cast<void*>(bodyCapturePoint);
    targets->cameraCapturePoint = reinterpret_cast<void*>(cameraCapturePoint);
    targets->vrUpdate = reinterpret_cast<void*>(vrUpdate);
    return true;
}

static void CopyMatrix(float* target, const float* source)
{
    std::memcpy(target, source, sizeof(float) * 16);
}

extern "C" void CapturePlayerBodyMatrix(uintptr_t liveBase)
{
    const float* matrix = reinterpret_cast<const float*>(liveBase + BODY_MATRIX_R15_OFFSET);
    CopyMatrix(g_latestBodyMatrix, matrix);
    InterlockedExchange(&g_hasBodyMatrix, 1);
}

extern "C" void CapturePlayerCameraMatrix(uintptr_t liveBase)
{
    const float* matrix = reinterpret_cast<const float*>(liveBase + CAMERA_MATRIX_R15_OFFSET);
    CopyMatrix(g_latestCameraMatrix, matrix);
    InterlockedExchange(&g_hasCameraMatrix, 1);
}

extern "C" __attribute__((naked)) void HookBodyCapturePoint()
{
    __asm__ __volatile__(
        ".intel_syntax noprefix\n"
        "pushfq\n"
        "push rax\n"
        "push rcx\n"
        "push rdx\n"
        "push rbx\n"
        "push rbp\n"
        "push rsi\n"
        "push rdi\n"
        "push r8\n"
        "push r9\n"
        "push r10\n"
        "push r11\n"
        "push r12\n"
        "push r13\n"
        "push r14\n"
        "push r15\n"
        "sub rsp, 0x100\n"
        "movdqu [rsp + 0x00], xmm0\n"
        "movdqu [rsp + 0x10], xmm1\n"
        "movdqu [rsp + 0x20], xmm2\n"
        "movdqu [rsp + 0x30], xmm3\n"
        "movdqu [rsp + 0x40], xmm4\n"
        "movdqu [rsp + 0x50], xmm5\n"
        "movdqu [rsp + 0x60], xmm6\n"
        "movdqu [rsp + 0x70], xmm7\n"
        "movdqu [rsp + 0x80], xmm8\n"
        "movdqu [rsp + 0x90], xmm9\n"
        "movdqu [rsp + 0xa0], xmm10\n"
        "movdqu [rsp + 0xb0], xmm11\n"
        "movdqu [rsp + 0xc0], xmm12\n"
        "movdqu [rsp + 0xd0], xmm13\n"
        "movdqu [rsp + 0xe0], xmm14\n"
        "movdqu [rsp + 0xf0], xmm15\n"
        "sub rsp, 0x20\n"
        "mov rcx, r15\n"
        "call CapturePlayerBodyMatrix\n"
        "add rsp, 0x20\n"
        "movdqu xmm0, [rsp + 0x00]\n"
        "movdqu xmm1, [rsp + 0x10]\n"
        "movdqu xmm2, [rsp + 0x20]\n"
        "movdqu xmm3, [rsp + 0x30]\n"
        "movdqu xmm4, [rsp + 0x40]\n"
        "movdqu xmm5, [rsp + 0x50]\n"
        "movdqu xmm6, [rsp + 0x60]\n"
        "movdqu xmm7, [rsp + 0x70]\n"
        "movdqu xmm8, [rsp + 0x80]\n"
        "movdqu xmm9, [rsp + 0x90]\n"
        "movdqu xmm10, [rsp + 0xa0]\n"
        "movdqu xmm11, [rsp + 0xb0]\n"
        "movdqu xmm12, [rsp + 0xc0]\n"
        "movdqu xmm13, [rsp + 0xd0]\n"
        "movdqu xmm14, [rsp + 0xe0]\n"
        "movdqu xmm15, [rsp + 0xf0]\n"
        "add rsp, 0x100\n"
        "pop r15\n"
        "pop r14\n"
        "pop r13\n"
        "pop r12\n"
        "pop r11\n"
        "pop r10\n"
        "pop r9\n"
        "pop r8\n"
        "pop rdi\n"
        "pop rsi\n"
        "pop rbp\n"
        "pop rbx\n"
        "pop rdx\n"
        "pop rcx\n"
        "pop rax\n"
        "popfq\n"
        "jmp qword ptr [rip + g_originalBodyCapturePoint]\n"
        ".att_syntax prefix\n"
    );
}

extern "C" __attribute__((naked)) void HookCameraCapturePoint()
{
    __asm__ __volatile__(
        ".intel_syntax noprefix\n"
        "pushfq\n"
        "push rax\n"
        "push rcx\n"
        "push rdx\n"
        "push rbx\n"
        "push rbp\n"
        "push rsi\n"
        "push rdi\n"
        "push r8\n"
        "push r9\n"
        "push r10\n"
        "push r11\n"
        "push r12\n"
        "push r13\n"
        "push r14\n"
        "push r15\n"
        "sub rsp, 0x100\n"
        "movdqu [rsp + 0x00], xmm0\n"
        "movdqu [rsp + 0x10], xmm1\n"
        "movdqu [rsp + 0x20], xmm2\n"
        "movdqu [rsp + 0x30], xmm3\n"
        "movdqu [rsp + 0x40], xmm4\n"
        "movdqu [rsp + 0x50], xmm5\n"
        "movdqu [rsp + 0x60], xmm6\n"
        "movdqu [rsp + 0x70], xmm7\n"
        "movdqu [rsp + 0x80], xmm8\n"
        "movdqu [rsp + 0x90], xmm9\n"
        "movdqu [rsp + 0xa0], xmm10\n"
        "movdqu [rsp + 0xb0], xmm11\n"
        "movdqu [rsp + 0xc0], xmm12\n"
        "movdqu [rsp + 0xd0], xmm13\n"
        "movdqu [rsp + 0xe0], xmm14\n"
        "movdqu [rsp + 0xf0], xmm15\n"
        "sub rsp, 0x20\n"
        "mov rcx, r15\n"
        "call CapturePlayerCameraMatrix\n"
        "add rsp, 0x20\n"
        "movdqu xmm0, [rsp + 0x00]\n"
        "movdqu xmm1, [rsp + 0x10]\n"
        "movdqu xmm2, [rsp + 0x20]\n"
        "movdqu xmm3, [rsp + 0x30]\n"
        "movdqu xmm4, [rsp + 0x40]\n"
        "movdqu xmm5, [rsp + 0x50]\n"
        "movdqu xmm6, [rsp + 0x60]\n"
        "movdqu xmm7, [rsp + 0x70]\n"
        "movdqu xmm8, [rsp + 0x80]\n"
        "movdqu xmm9, [rsp + 0x90]\n"
        "movdqu xmm10, [rsp + 0xa0]\n"
        "movdqu xmm11, [rsp + 0xb0]\n"
        "movdqu xmm12, [rsp + 0xc0]\n"
        "movdqu xmm13, [rsp + 0xd0]\n"
        "movdqu xmm14, [rsp + 0xe0]\n"
        "movdqu xmm15, [rsp + 0xf0]\n"
        "add rsp, 0x100\n"
        "pop r15\n"
        "pop r14\n"
        "pop r13\n"
        "pop r12\n"
        "pop r11\n"
        "pop r10\n"
        "pop r9\n"
        "pop r8\n"
        "pop rdi\n"
        "pop rsi\n"
        "pop rbp\n"
        "pop rbx\n"
        "pop rdx\n"
        "pop rcx\n"
        "pop rax\n"
        "popfq\n"
        "jmp qword ptr [rip + g_originalCameraCapturePoint]\n"
        ".att_syntax prefix\n"
    );
}

static bool TryGetCameraRelativeHudMatrix(float* hud)
{
    float offsetZ = InterlockedCompareExchange(&g_hasHudOffsetZ, 1, 1)
        ? g_latestHudOffsetZ
        : DEFAULT_HUD_OFFSET_Z;

    if (!InterlockedCompareExchange(&g_hasBodyMatrix, 1, 1))
    {
        return BuildCameraRelativeHudMatrixWithFallback(
            nullptr,
            nullptr,
            &g_hudMatrixState,
            hud,
            HUD_OFFSET_X,
            HUD_OFFSET_Y,
            offsetZ
        );
    }

    if (!InterlockedCompareExchange(&g_hasCameraMatrix, 1, 1))
    {
        return BuildCameraRelativeHudMatrixWithFallback(
            nullptr,
            nullptr,
            &g_hudMatrixState,
            hud,
            HUD_OFFSET_X,
            HUD_OFFSET_Y,
            offsetZ
        );
    }

    float body[16] = {};
    float camera[16] = {};
    CopyMatrix(body, g_latestBodyMatrix);
    CopyMatrix(camera, g_latestCameraMatrix);

    return BuildCameraRelativeHudMatrixWithFallback(
        camera,
        body,
        &g_hudMatrixState,
        hud,
        HUD_OFFSET_X,
        HUD_OFFSET_Y,
        offsetZ
    );
}

static void WINAPI HookApplyMatrix(uint32_t handle, float* matrix)
{
    if (handle == TARGET_HUD_HANDLE)
    {
        float offsetZ = 0.0f;
        if (ExtractLocalHudOffsetZ(matrix, &offsetZ))
        {
            g_latestHudOffsetZ = offsetZ;
            InterlockedExchange(&g_hasHudOffsetZ, 1);
        }

        float hud[16] = {};
        if (TryGetCameraRelativeHudMatrix(hud))
        {
            g_originalApplyMatrix(handle, hud);
            return;
        }
    }

    g_originalApplyMatrix(handle, matrix);
}

static void ApplyCameraRelativeHudMatrix()
{
    if (!g_originalApplyMatrix)
        return;

    float hud[16] = {};
    if (!TryGetCameraRelativeHudMatrix(hud))
        return;

    g_originalApplyMatrix(TARGET_HUD_HANDLE, hud);
}

static void WINAPI HookVrUpdate(void* self)
{
    g_originalVrUpdate(self);
    ApplyCameraRelativeHudMatrix();
}

static bool CreateHook(const char* name, void* target, void* hook, void** original)
{
    MH_STATUS status = MH_CreateHook(target, hook, original);
    if (status != MH_OK)
    {
        DebugLogFormat("MH_CreateHook failed for %s: %d", name, static_cast<int>(status));
        return false;
    }

    status = MH_EnableHook(target);
    if (status != MH_OK)
    {
        DebugLogFormat("MH_EnableHook failed for %s: %d", name, static_cast<int>(status));
        MH_RemoveHook(target);
        return false;
    }

    return true;
}

static void RemoveInstalledHooks(void** targets, size_t count)
{
    for (size_t i = 0; i < count; ++i)
    {
        MH_DisableHook(targets[i]);
        MH_RemoveHook(targets[i]);
    }
}

static DWORD WINAPI InstallHooksThread(LPVOID)
{
    Sleep(2000);

    HookTargets targets = {};
    if (!ResolveHookTargets(&targets))
    {
        DebugLog("hook target signature resolution failed; no hooks installed");
        return 0;
    }

    MH_STATUS status = MH_Initialize();
    if (status != MH_OK && status != MH_ERROR_ALREADY_INITIALIZED)
    {
        DebugLogFormat("MH_Initialize failed: %d", static_cast<int>(status));
        return 0;
    }

    void* installedHooks[4] = {};
    size_t installedHookCount = 0;

    if (!CreateHook(
        "APPLY_MATRIX",
        targets.applyMatrix,
        reinterpret_cast<void*>(&HookApplyMatrix),
        reinterpret_cast<void**>(&g_originalApplyMatrix)
    ))
        return 0;
    installedHooks[installedHookCount++] = targets.applyMatrix;

    if (!CreateHook(
        "BODY_CAPTURE_POINT",
        targets.bodyCapturePoint,
        reinterpret_cast<void*>(&HookBodyCapturePoint),
        reinterpret_cast<void**>(&g_originalBodyCapturePoint)
    ))
    {
        RemoveInstalledHooks(installedHooks, installedHookCount);
        return 0;
    }
    installedHooks[installedHookCount++] = targets.bodyCapturePoint;

    if (!CreateHook(
        "CAMERA_CAPTURE_POINT",
        targets.cameraCapturePoint,
        reinterpret_cast<void*>(&HookCameraCapturePoint),
        reinterpret_cast<void**>(&g_originalCameraCapturePoint)
    ))
    {
        RemoveInstalledHooks(installedHooks, installedHookCount);
        return 0;
    }
    installedHooks[installedHookCount++] = targets.cameraCapturePoint;

    if (!CreateHook(
        "VR_UPDATE",
        targets.vrUpdate,
        reinterpret_cast<void*>(&HookVrUpdate),
        reinterpret_cast<void**>(&g_originalVrUpdate)
    ))
    {
        RemoveInstalledHooks(installedHooks, installedHookCount);
        return 0;
    }

    return 0;
}

BOOL WINAPI DllMain(HINSTANCE module, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        DisableThreadLibraryCalls(module);

        HANDLE thread = CreateThread(nullptr, 0, InstallHooksThread, nullptr, 0, nullptr);
        if (thread)
            CloseHandle(thread);
    }

    return TRUE;
}
