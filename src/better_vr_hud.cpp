#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cstdint>
#include <cstring>

#include "MinHook.h"
#include "better_vr_hud_math.h"

static constexpr uintptr_t APPLY_MATRIX_OFFSET = 0x1838500;
static constexpr uintptr_t BODY_CAPTURE_POINT_OFFSET = 0x3377BD;
static constexpr uintptr_t CAMERA_CAPTURE_POINT_OFFSET = 0x337927;
static constexpr uintptr_t VR_UPDATE_OFFSET = 0x2C356C0;

static constexpr uintptr_t BODY_MATRIX_R15_OFFSET = 0x5B0;
static constexpr uintptr_t CAMERA_MATRIX_R15_OFFSET = 0x510;
static constexpr uint32_t TARGET_HUD_HANDLE = 0x00080133;
static constexpr float HUD_OFFSET_X = 0.0f;
static constexpr float HUD_OFFSET_Y = 0.0f;
static constexpr float HUD_OFFSET_Z = -2.5f;

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
static volatile LONG g_hasBodyMatrix = 0;
static volatile LONG g_hasCameraMatrix = 0;

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
    if (!InterlockedCompareExchange(&g_hasBodyMatrix, 1, 1))
    {
        return BuildCameraRelativeHudMatrixWithFallback(
            nullptr,
            nullptr,
            &g_hudMatrixState,
            hud,
            HUD_OFFSET_X,
            HUD_OFFSET_Y,
            HUD_OFFSET_Z
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
            HUD_OFFSET_Z
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
        HUD_OFFSET_Z
    );
}

static void WINAPI HookApplyMatrix(uint32_t handle, float* matrix)
{
    if (handle == TARGET_HUD_HANDLE)
    {
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

static bool CreateHook(void* target, void* hook, void** original)
{
    MH_STATUS status = MH_CreateHook(target, hook, original);
    if (status != MH_OK)
        return false;

    status = MH_EnableHook(target);
    return status == MH_OK;
}

static DWORD WINAPI InstallHooksThread(LPVOID)
{
    Sleep(2000);

    uintptr_t base = reinterpret_cast<uintptr_t>(GetModuleHandleA(nullptr));

    MH_STATUS status = MH_Initialize();
    if (status != MH_OK && status != MH_ERROR_ALREADY_INITIALIZED)
        return 0;

    CreateHook(
        reinterpret_cast<void*>(base + APPLY_MATRIX_OFFSET),
        reinterpret_cast<void*>(&HookApplyMatrix),
        reinterpret_cast<void**>(&g_originalApplyMatrix)
    );

    CreateHook(
        reinterpret_cast<void*>(base + BODY_CAPTURE_POINT_OFFSET),
        reinterpret_cast<void*>(&HookBodyCapturePoint),
        reinterpret_cast<void**>(&g_originalBodyCapturePoint)
    );

    CreateHook(
        reinterpret_cast<void*>(base + CAMERA_CAPTURE_POINT_OFFSET),
        reinterpret_cast<void*>(&HookCameraCapturePoint),
        reinterpret_cast<void**>(&g_originalCameraCapturePoint)
    );

    CreateHook(
        reinterpret_cast<void*>(base + VR_UPDATE_OFFSET),
        reinterpret_cast<void*>(&HookVrUpdate),
        reinterpret_cast<void**>(&g_originalVrUpdate)
    );

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
