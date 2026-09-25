#include "common/log.h"

#include <cstdarg>
#include <cstdio>
#include <cwchar>

namespace bvh
{
    namespace
    {
        CRITICAL_SECTION g_lock;
        bool g_lockReady = false;
        FILE* g_file = nullptr;
        wchar_t g_path[MAX_PATH] = L"";
    }

    bool LogOpen(HMODULE module, const wchar_t* baseName, const wchar_t* extension, bool timestamped)
    {
        if (!g_lockReady)
        {
            InitializeCriticalSection(&g_lock);
            g_lockReady = true;
        }

        wchar_t directory[MAX_PATH] = L"";
        DWORD length = GetModuleFileNameW(module, directory, MAX_PATH);
        if (length == 0 || length >= MAX_PATH)
            return false;

        wchar_t* slash = std::wcsrchr(directory, L'\\');
        if (slash)
            slash[1] = L'\0';

        if (timestamped)
        {
            SYSTEMTIME now = {};
            GetLocalTime(&now);
            std::swprintf(
                g_path, MAX_PATH, L"%ls%ls_%04u%02u%02u_%02u%02u%02u.%ls",
                directory, baseName,
                now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond,
                extension
            );
        }
        else
        {
            std::swprintf(g_path, MAX_PATH, L"%ls%ls.%ls", directory, baseName, extension);
        }

        g_file = _wfopen(g_path, L"wb");
        if (!g_file)
            return false;

        std::setvbuf(g_file, nullptr, _IOFBF, 1 << 20);
        return true;
    }

    void LogWrite(const char* text, size_t length)
    {
        if (!g_file)
            return;

        EnterCriticalSection(&g_lock);
        std::fwrite(text, 1, length, g_file);
        LeaveCriticalSection(&g_lock);
    }

    void Logf(const char* format, ...)
    {
        char buffer[4096];

        va_list args;
        va_start(args, format);
        int length = std::vsnprintf(buffer, sizeof(buffer), format, args);
        va_end(args);

        if (length < 0)
            return;
        if (length >= static_cast<int>(sizeof(buffer)))
            length = sizeof(buffer) - 1;

        LogWrite(buffer, static_cast<size_t>(length));
    }

    void LogFlush()
    {
        if (!g_file)
            return;

        EnterCriticalSection(&g_lock);
        std::fflush(g_file);
        LeaveCriticalSection(&g_lock);
    }

    void LogClose()
    {
        if (!g_file)
            return;

        EnterCriticalSection(&g_lock);
        std::fclose(g_file);
        g_file = nullptr;
        LeaveCriticalSection(&g_lock);
    }

    const wchar_t* LogPath()
    {
        return g_path;
    }

    int64_t NowMicros()
    {
        static LARGE_INTEGER frequency = {};
        static LARGE_INTEGER start = {};

        if (frequency.QuadPart == 0)
        {
            QueryPerformanceFrequency(&frequency);
            QueryPerformanceCounter(&start);
        }

        LARGE_INTEGER now;
        QueryPerformanceCounter(&now);
        return (now.QuadPart - start.QuadPart) * 1000000 / frequency.QuadPart;
    }
}
