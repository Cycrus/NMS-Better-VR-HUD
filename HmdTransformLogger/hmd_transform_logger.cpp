#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <cstring>

static HANDLE g_stopEvent = nullptr;
static HANDLE g_loggerThread = nullptr;
static ULONGLONG g_startTick = 0;

static constexpr uintptr_t HMD_ROLL_OFFSET = 0x6DFECD0;
static constexpr uintptr_t HMD_PITCH_OFFSET = 0x6DFECD8;
static constexpr uintptr_t HMD_YAW_OFFSET = 0x6DFECE0;

static bool IsReadableAddress(const void* address)
{
    MEMORY_BASIC_INFORMATION info = {};
    if (!VirtualQuery(address, &info, sizeof(info)))
        return false;

    if (info.State != MEM_COMMIT)
        return false;

    DWORD protect = info.Protect & 0xff;
    return protect == PAGE_READONLY ||
        protect == PAGE_READWRITE ||
        protect == PAGE_WRITECOPY ||
        protect == PAGE_EXECUTE_READ ||
        protect == PAGE_EXECUTE_READWRITE ||
        protect == PAGE_EXECUTE_WRITECOPY;
}

static bool ReadFloat(uintptr_t address, float* value)
{
    if (!IsReadableAddress(reinterpret_cast<const void*>(address)))
        return false;

    *value = *reinterpret_cast<volatile const float*>(address);
    return true;
}

static void AppendCsvLine(const char* line)
{
    HANDLE file = CreateFileA(
        "/home/cyril/Downloads/hmd_readings.csv",
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
        const char header[] = "elapsed_ms,yaw,roll,pitch\r\n";
        DWORD written = 0;
        WriteFile(file, header, sizeof(header) - 1, &written, nullptr);
    }

    DWORD written = 0;
    WriteFile(file, line, static_cast<DWORD>(std::strlen(line)), &written, nullptr);
    CloseHandle(file);
}

static DWORD WINAPI LoggerThread(LPVOID)
{
    HMODULE nms = GetModuleHandleA(nullptr);

    while ( WaitForSingleObject(g_stopEvent, 100) == WAIT_TIMEOUT)
    {
        uintptr_t base = reinterpret_cast<uintptr_t>(nms);
        float yaw = 0.0f;
        float roll = 0.0f;
        float pitch = 0.0f;

        if (!ReadFloat(base + HMD_YAW_OFFSET, &yaw) ||
            !ReadFloat(base + HMD_ROLL_OFFSET, &roll) ||
            !ReadFloat(base + HMD_PITCH_OFFSET, &pitch))
        {
            continue;
        }

        char line[128];
        std::snprintf(
            line,
            sizeof(line),
            "%llu,%.9g,%.9g,%.9g\r\n",
            GetTickCount64() - g_startTick,
            yaw,
            roll,
            pitch
        );

        AppendCsvLine(line);
    }

    return 0;
}

BOOL WINAPI DllMain(HINSTANCE module, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        DisableThreadLibraryCalls(module);

        g_startTick = GetTickCount64();
        g_stopEvent = CreateEventA(nullptr, TRUE, FALSE, nullptr);

        if (g_stopEvent)
            g_loggerThread = CreateThread(nullptr, 0, LoggerThread, nullptr, 0, nullptr);
    }
    else if (reason == DLL_PROCESS_DETACH)
    {
        if (g_stopEvent)
            SetEvent(g_stopEvent);

        if (g_loggerThread)
        {
            WaitForSingleObject(g_loggerThread, 2000);
            CloseHandle(g_loggerThread);
        }

        if (g_stopEvent)
            CloseHandle(g_stopEvent);
    }

    return TRUE;
}
