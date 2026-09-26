/* Logs the HUD transform matrix while playing into a csv file.
 * The output file is ~/Downloads/hud_transform_readings.csv.
 */

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cstdint>
#include <cstdarg>
#include <cstdio>
#include <cstring>

#include "MinHook.h"

static constexpr uintptr_t APPLY_MATRIX_OFFSET = 0x1838500;
static constexpr uint32_t TARGET_HUD_HANDLE = 0x00080133;

using ApplyMatrixFn = void (WINAPI*)(uint32_t handle, float* matrix);

static HINSTANCE g_module = nullptr;
static uintptr_t g_base = 0;
static ApplyMatrixFn g_originalApplyMatrix = nullptr;
static ULONGLONG g_startTick = 0;
static volatile LONG g_hudHitCount = 0;

static void AppendFileLine(const char* line)
{
    char path[MAX_PATH] = "~/Downloads/hud_transform_readings.csv\0";

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

static void EnsureCsvHeader()
{
    char path[MAX_PATH] = "~/Downloads/hud_transform_readings.csv\0";

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

    LARGE_INTEGER size = {};
    if (GetFileSizeEx(file, &size) && size.QuadPart == 0)
    {
        const char header[] =
            "elapsed_ms,sample,handle,m00,m01,m02,m03,m10,m11,m12,m13,m20,m21,m22,m23,m30,m31,m32,m33\r\n";
        DWORD written = 0;
        WriteFile(file, header, sizeof(header) - 1, &written, nullptr);
    }

    CloseHandle(file);
}

static void AppendStatusLine(const char* line)
{
    char path[MAX_PATH] = "~/Downloads/hud_transform_logger.log\0";

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

static void LogStatusFormat(const char* format, ...)
{
    char line[512];

    va_list args;
    va_start(args, format);
    std::vsnprintf(line, sizeof(line), format, args);
    va_end(args);

    AppendStatusLine(line);
}

static void AppendMatrixCsv(ULONGLONG elapsed, LONG sample, uint32_t handle, const float* matrix)
{
    if (!matrix)
        return;

    char line[512];
    std::snprintf(
        line,
        sizeof(line),
        "%llu,%ld,0x%08x,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g\r\n",
        elapsed,
        sample,
        handle,
        matrix[0],
        matrix[1],
        matrix[2],
        matrix[3],
        matrix[4],
        matrix[5],
        matrix[6],
        matrix[7],
        matrix[8],
        matrix[9],
        matrix[10],
        matrix[11],
        matrix[12],
        matrix[13],
        matrix[14],
        matrix[15]
    );

    AppendFileLine(line);
}

static void WINAPI HookApplyMatrix(uint32_t handle, float* matrix)
{
    if (handle == TARGET_HUD_HANDLE && matrix)
    {
        LONG sample = InterlockedIncrement(&g_hudHitCount);
        ULONGLONG elapsed = GetTickCount64() - g_startTick;
        AppendMatrixCsv(elapsed, sample, handle, matrix);
    }

    g_originalApplyMatrix(handle, matrix);
}

static DWORD WINAPI InstallHookThread(LPVOID)
{
    Sleep(2000);

    g_base = reinterpret_cast<uintptr_t>(GetModuleHandleA(nullptr));
    void* target = reinterpret_cast<void*>(g_base + APPLY_MATRIX_OFFSET);

    EnsureCsvHeader();

    MH_STATUS status = MH_Initialize();
    if (status != MH_OK && status != MH_ERROR_ALREADY_INITIALIZED)
    {
        LogStatusFormat("MH_Initialize failed: %d\r\n", static_cast<int>(status));
        return 0;
    }

    status = MH_CreateHook(
        target,
        reinterpret_cast<void*>(&HookApplyMatrix),
        reinterpret_cast<void**>(&g_originalApplyMatrix)
    );

    if (status != MH_OK)
    {
        LogStatusFormat("MH_CreateHook failed: %d\r\n", static_cast<int>(status));
        return 0;
    }

    status = MH_EnableHook(target);
    if (status != MH_OK)
    {
        LogStatusFormat("MH_EnableHook failed: %d\r\n", static_cast<int>(status));
        return 0;
    }

    LogStatusFormat(
        "Hook enabled: base=%p target=%p target_handle=0x%08x\r\n",
        reinterpret_cast<void*>(g_base),
        target,
        TARGET_HUD_HANDLE
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

        HANDLE thread = CreateThread(nullptr, 0, InstallHookThread, nullptr, 0, nullptr);
        if (thread)
            CloseHandle(thread);
    }

    return TRUE;
}
