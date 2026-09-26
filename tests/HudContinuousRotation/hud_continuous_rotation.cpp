/* Rotates the hud continuously around the y axis.
 */

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cmath>
#include <cstdint>
#include <cstdarg>
#include <cstdio>
#include <cstring>

#include "MinHook.h"

static constexpr uintptr_t APPLY_MATRIX_OFFSET = 0x1838500;
static constexpr uintptr_t VR_UPDATE_OFFSET = 0x2C356C0;

static constexpr uint32_t TARGET_HUD_HANDLE = 0x00080133;
static constexpr float DEFAULT_X = 0.0f;
static constexpr float DEFAULT_Y = 0.0f;
static constexpr float DEFAULT_Z = -2.5f;
static constexpr ULONGLONG ROTATION_PERIOD_MS = 10000;
static constexpr float TWO_PI = 6.2831853071795864769f;

using ApplyMatrixFn = void (WINAPI*)(uint32_t handle, float* matrix);
using VrUpdateFn = void (WINAPI*)(void* self);

static HINSTANCE g_module = nullptr;
static ApplyMatrixFn g_applyMatrix = nullptr;
static VrUpdateFn g_originalVrUpdate = nullptr;
static ULONGLONG g_startTick = 0;
static volatile LONG g_tickApplyCount = 0;

static void LogFormat(const char* format, ...)
{
    char line[512];

    va_list args;
    va_start(args, format);
    std::vsnprintf(line, sizeof(line), format, args);
    va_end(args);
}

static float CurrentRotationRadians()
{
    ULONGLONG elapsed = GetTickCount64() - g_startTick;
    ULONGLONG phase = elapsed % ROTATION_PERIOD_MS;
    return (static_cast<float>(phase) / static_cast<float>(ROTATION_PERIOD_MS)) * TWO_PI;
}

static void ApplyContinuousHudRotation()
{
    if (!g_applyMatrix)
        return;

    float angle = CurrentRotationRadians();
    float c = std::cos(angle);
    float s = std::sin(angle);

    float matrix[16] = {
        c, 0.0f, -s, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        s, 0.0f, c, 0.0f,
        DEFAULT_X, DEFAULT_Y, DEFAULT_Z, 1.0f
    };

    g_applyMatrix(TARGET_HUD_HANDLE, matrix);

    LONG count = InterlockedIncrement(&g_tickApplyCount);
    if (count <= 10 || (count % 300) == 0)
    {
        LogFormat(
            "Applied continuous rotation #%ld angle=%.6f pos=(%.6f, %.6f, %.6f) row0=(%.6f, %.6f, %.6f)\r\n",
            count,
            angle,
            matrix[12],
            matrix[13],
            matrix[14],
            matrix[0],
            matrix[1],
            matrix[2]
        );
    }
}

static void WINAPI HookVrUpdate(void* self)
{
    g_originalVrUpdate(self);
    ApplyContinuousHudRotation();
}

static bool CreateHook(void* target, void* hook, void** original, const char* name)
{
    MH_STATUS status = MH_CreateHook(target, hook, original);
    if (status != MH_OK)
    {
        LogFormat("MH_CreateHook failed for %s: %d\r\n", name, static_cast<int>(status));
        return false;
    }

    status = MH_EnableHook(target);
    if (status != MH_OK)
    {
        LogFormat("MH_EnableHook failed for %s: %d\r\n", name, static_cast<int>(status));
        return false;
    }

    LogFormat("Hook enabled for %s at %p\r\n", name, target);
    return true;
}

static DWORD WINAPI InstallHooksThread(LPVOID)
{
    Sleep(2000);

    uintptr_t base = reinterpret_cast<uintptr_t>(GetModuleHandleA(nullptr));
    void* vrUpdateTarget = reinterpret_cast<void*>(base + VR_UPDATE_OFFSET);
    g_applyMatrix = reinterpret_cast<ApplyMatrixFn>(base + APPLY_MATRIX_OFFSET);

    LogFormat(
        "Installing hooks: base=%p apply_matrix=%p vr_update=%p target_handle=0x%08x period_ms=%llu\r\n",
        reinterpret_cast<void*>(base),
        reinterpret_cast<void*>(g_applyMatrix),
        vrUpdateTarget,
        TARGET_HUD_HANDLE,
        ROTATION_PERIOD_MS
    );

    MH_STATUS status = MH_Initialize();
    if (status != MH_OK && status != MH_ERROR_ALREADY_INITIALIZED)
    {
        LogFormat("MH_Initialize failed: %d\r\n", static_cast<int>(status));
        return 0;
    }

    CreateHook(
        vrUpdateTarget,
        reinterpret_cast<void*>(&HookVrUpdate),
        reinterpret_cast<void**>(&g_originalVrUpdate),
        "VrUpdate"
    );

    return 0;
}

BOOL WINAPI DllMain(HINSTANCE module, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        g_module = module;
        g_startTick = GetTickCount64();
        DisableThreadLibraryCalls(module);

        HANDLE thread = CreateThread(nullptr, 0, InstallHooksThread, nullptr, 0, nullptr);
        if (thread)
            CloseHandle(thread);
    }

    return TRUE;
}
