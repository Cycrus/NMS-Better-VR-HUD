#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <cstring>

#include "MinHook.h"

static constexpr uintptr_t APPLY_MATRIX_OFFSET = 0x1838500;
static constexpr uintptr_t HUD_CALL_RETURN_OFFSET = 0xAB1CA2;

using ApplyMatrixFn = void (WINAPI*)(uint32_t handle, float* matrix);

static HINSTANCE g_module = nullptr;
static uintptr_t g_base = 0;
static void* g_hudCallReturnAddress = nullptr;
static ApplyMatrixFn g_originalApplyMatrix = nullptr;
static volatile LONG g_hudHitCount = 0;

static void LogLine(const char* line)
{
    char path[MAX_PATH] = "/home/cyril/Downloads/hud_prober.log\0";

    HANDLE file = CreateFileA(
        path,
        FILE_APPEND_DATA,
        FILE_SHARE_READ,
        nullptr,
        OPEN_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        nullptr
    );

    if (file == INVALID_HANDLE_VALUE)
        return;

    DWORD written = 0;
    WriteFile(file, line, static_cast<DWORD>(std::strlen(line)), &written, nullptr);
    CloseHandle(file);
}

static void LogFormat(const char* format, ...)
{
    char line[512];

    va_list args;
    va_start(args, format);
    std::vsnprintf(line, sizeof(line), format, args);
    va_end(args);

    LogLine(line);
}

static void WINAPI HookApplyMatrix(uint32_t handle, float* matrix)
{
    void* returnAddress = __builtin_return_address(0);

    if(matrix)
    {
        LONG count = InterlockedIncrement(&g_hudHitCount);
        if (count <= 20 || (count % 300) == 0)
        {
            LogFormat(
                "Other hit -- handle=0x%08x pos=(%.6f, %.6f, %.6f) ret=(%p)\r\n",
                handle,
                matrix[12],
                matrix[13],
                matrix[14],
                returnAddress
            );
        }
    }

    g_originalApplyMatrix(handle, matrix);
}

static DWORD WINAPI InstallHookThread(LPVOID)
{
    Sleep(2000);

    g_base = reinterpret_cast<uintptr_t>(GetModuleHandleA(nullptr));
    g_hudCallReturnAddress = reinterpret_cast<void*>(g_base + HUD_CALL_RETURN_OFFSET);
    void* target = reinterpret_cast<void*>(g_base + APPLY_MATRIX_OFFSET);

    LogFormat(
        "Installing hook: base=%p target=%p hud_return=%p\r\n",
        reinterpret_cast<void*>(g_base),
        target,
        g_hudCallReturnAddress
    );

    MH_STATUS status = MH_Initialize();
    if (status != MH_OK && status != MH_ERROR_ALREADY_INITIALIZED)
    {
        LogFormat("MH_Initialize failed: %d\r\n", static_cast<int>(status));
        return 0;
    }

    status = MH_CreateHook(
        target,
        reinterpret_cast<void*>(&HookApplyMatrix),
        reinterpret_cast<void**>(&g_originalApplyMatrix)
    );

    if (status != MH_OK)
    {
        LogFormat("MH_CreateHook failed: %d\r\n", static_cast<int>(status));
        return 0;
    }

    status = MH_EnableHook(target);
    if (status != MH_OK)
    {
        LogFormat("MH_EnableHook failed: %d\r\n", static_cast<int>(status));
        return 0;
    }

    LogLine("Hook enabled.\r\n");
    return 0;
}

BOOL WINAPI DllMain(HINSTANCE module, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        g_module = module;
        DisableThreadLibraryCalls(module);
        LogLine("HudTransformTest attached.\r\n");

        HANDLE thread = CreateThread(nullptr, 0, InstallHookThread, nullptr, 0, nullptr);
        if (thread)
            CloseHandle(thread);
    }
    else if (reason == DLL_PROCESS_DETACH)
    {
        LogLine("HudTransformTest detached.\r\n");
    }

    return TRUE;
}
