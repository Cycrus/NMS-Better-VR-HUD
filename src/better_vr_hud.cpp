/* The main module for the better VR HUD mod.
 */

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "MinHook.h"
#include "math.h"
#include "nms_signatures.h"

#ifdef LEVEL_HUD
static HudType hud_type = HudType::LEVEL;
static constexpr float HUD_OFFSET_Y = 0.0f;
#else
static HudType hud_type = HudType::FULL;
static constexpr float HUD_OFFSET_Y = -0.5f;
#endif

static constexpr uintptr_t BODY_MATRIX_R15_OFFSET = 0x5B0;
static constexpr uintptr_t CAMERA_MATRIX_R15_OFFSET = 0x510;
static constexpr uint32_t TARGET_HUD_HANDLE = 0x00080133;
static constexpr float HUD_OFFSET_X = 0.0f;

static constexpr float DEFAULT_HUD_OFFSET_Z = -2.5f;

using ApplyMatrixFn = void (WINAPI*)(uint32_t handle, float* matrix);
using VrUpdateFn = void (WINAPI*)(void* self);

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

/**
 * Simply copies a transform matrix from one location to another.
 */
static void CopyMatrix(float* target, const float* source)
{
    std::memcpy(target, source, sizeof(float) * 16);
}

/**
 * Copies a transform matrix stored at the address of liveBase into the global g_latestBodyMatrix.
 * Main path to update the body transform matrix used as a static anchor for the coordinate system
 * projection between head rotation and hud matrix.
 */
extern "C" void CapturePlayerBodyMatrix(uintptr_t liveBase)
{
    const float* matrix = reinterpret_cast<const float*>(liveBase + BODY_MATRIX_R15_OFFSET);
    CopyMatrix(g_latestBodyMatrix, matrix);
    InterlockedExchange(&g_hasBodyMatrix, 1);
}

/**
 * Copies a transform matrix stored at the address of liveBase into the global g_latestCameraMatrix.
 * Main path to update the camera transform matrix used to orient the HUD rotation and position against.
 */
extern "C" void CapturePlayerCameraMatrix(uintptr_t liveBase)
{
    const float* matrix = reinterpret_cast<const float*>(liveBase + CAMERA_MATRIX_R15_OFFSET);
    CopyMatrix(g_latestCameraMatrix, matrix);
    InterlockedExchange(&g_hasCameraMatrix, 1);
}

/**
 * Captures the body transform matrix my manually pushing the matrix
 * registers into the CapturePlayerBodyMatrix arguments.
 */
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

/**
 * Captures the camera transform matrix my manually pushing the matrix
 * registers into the CapturePlayerCameraMatrix arguments.
 */
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

/**
 * Attempts to fetch the distance (z offset) of the HUD transform of the current frame.
 * Afterwards builds the new HUD transform with the updated orientation.
 * If no valid z body or camera matrix was found, the previous HUD transform is used.
 * TODO: This currently leads to occasional stuttering. This is likely caused by a data
 *       race between camera/body updates in the plugin and in the game.
 */
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
            offsetZ,
            hud_type
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
            offsetZ,
            hud_type
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
        offsetZ,
        hud_type
    );
}

/**
 * applyMatrix is called whenever something changes its transform in the game.
 * The target HUD handle is empirically detected and identifies the VR HUD.
 * We use this to inject our own computed HUD transform matrix whenever the game
 * wants to update the HUD transform. This only happens very occasionally and
 * is mainly done to avoid the HUD jumping back into its original transform
 * from time to time. The main HUD update path happens in HookVrUpdate and
 * ApplyCameraRelativeHudMatrix.
 */
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

/**
 * Updates the HUD transform matrix by manually calling applyMatrix method of the game
 * with the empirically found target HUD handle.
 */
static void ApplyCameraRelativeHudMatrix()
{
    if (!g_originalApplyMatrix)
        return;

    float hud[16] = {};
    if (!TryGetCameraRelativeHudMatrix(hud))
        return;

    g_originalApplyMatrix(TARGET_HUD_HANDLE, hud);
}

/**
 * Hooks the VrUpdate method of the game to update the HUD whenever a VR update happens.
 * This should happen every frame to achieve as fluid HUD movement as possible.
 */
static void WINAPI HookVrUpdate(void* self)
{
    g_originalVrUpdate(self);
    ApplyCameraRelativeHudMatrix();
}

/**
 * A helper function to inject a hook into a native game method.
 */
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

/**
 * A helper function to remove an injected hook from a native game method.
 */
static void RemoveInstalledHooks(void** targets, size_t count)
{
    for (size_t i = 0; i < count; ++i)
    {
        MH_DisableHook(targets[i]);
        MH_RemoveHook(targets[i]);
    }
}

/**
 * A thread worker concurrently installing the hooks after the game had some time to load.
 * This happens concurrently to avoid blocking any game processing.
 */
static DWORD WINAPI InstallHooksThread(LPVOID)
{
    Sleep(2000);

    HookTargets targets = {};
    if (!ResolveHookTargets(&targets, DebugLog))
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

/**
 * The main entry point of the mod.
 */
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
