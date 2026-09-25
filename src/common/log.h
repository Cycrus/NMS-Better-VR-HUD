#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>

namespace bvh
{
    // Opens "<directory of module>\<baseName>[_YYYYMMDD_HHMMSS].<extension>" for writing.
    bool LogOpen(HMODULE module, const wchar_t* baseName, const wchar_t* extension, bool timestamped);
    void LogWrite(const char* text, size_t length);
    void Logf(const char* format, ...) __attribute__((format(__MINGW_PRINTF_FORMAT, 1, 2)));
    void LogFlush();
    void LogClose();
    const wchar_t* LogPath();

    // Microseconds since the first call.
    int64_t NowMicros();
}
