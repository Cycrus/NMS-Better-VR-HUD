#include "signature_scanner.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#include <cstdarg>
#include <cstdio>
#include <cstring>

static void Log(SignatureScannerLogFn log, const char* message)
{
    if (log)
        log(message);
}

static void LogFormat(SignatureScannerLogFn log, const char* format, ...)
{
    if (!log)
        return;

    char buffer[512] = {};
    va_list args;
    va_start(args, format);
    std::vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);

    log(buffer);
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

bool CompilePattern(const char* text, CompiledPattern* pattern, SignatureScannerLogFn log)
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
            Log(log, "signature is longer than the local pattern buffer");
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
            Log(log, "signature contains an invalid byte token");
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
        Log(log, "signature is empty");
        return false;
    }

    return true;
}

bool GetMainModuleExecutableSections(ModuleExecutableSections* module, SignatureScannerLogFn log)
{
#ifdef _WIN32
    module->base = reinterpret_cast<uint8_t*>(GetModuleHandleA(nullptr));
    module->count = 0;

    if (!module->base)
    {
        Log(log, "GetModuleHandleA(nullptr) failed");
        return false;
    }

    auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(module->base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE)
    {
        Log(log, "main module has an invalid DOS header");
        return false;
    }

    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(module->base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE)
    {
        Log(log, "main module has an invalid NT header");
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
            Log(log, "main module has more executable sections than expected");
            return false;
        }

        uintptr_t start = reinterpret_cast<uintptr_t>(module->base) + section.VirtualAddress;
        uintptr_t end = start + sectionSize;
        if (end <= start)
        {
            Log(log, "main module executable section range overflowed");
            return false;
        }

        module->sections[module->count++] = {start, end};
    }

    if (module->count == 0)
    {
        Log(log, "main module has no executable sections");
        return false;
    }

    return true;
#else
    module->base = nullptr;
    module->count = 0;
    Log(log, "main module executable section scanning is only available on Windows");
    return false;
#endif
}

bool IsExecutableRange(
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

bool PatternMatches(const uint8_t* cursor, const CompiledPattern& pattern)
{
    for (size_t i = 0; i < pattern.length; ++i)
    {
        if (!pattern.bytes[i].wildcard && cursor[i] != pattern.bytes[i].value)
            return false;
    }

    return true;
}

bool FindUniquePattern(
    const ModuleExecutableSections& module,
    const char* name,
    const char* signature,
    uintptr_t* match,
    SignatureScannerLogFn log
)
{
    CompiledPattern pattern = {};
    if (!CompilePattern(signature, &pattern, log))
    {
        LogFormat(log, "%s signature failed to compile", name);
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
                LogFormat(log, "%s signature matched more than once", name);
                return false;
            }
        }
    }

    if (matchCount != 1)
    {
        LogFormat(log, "%s signature did not match", name);
        return false;
    }

    *match = found;
    return true;
}

bool ResolveCallRel32Target(
    const ModuleExecutableSections& module,
    const char* name,
    const char* signature,
    size_t callOffset,
    uintptr_t* target,
    SignatureScannerLogFn log
)
{
    uintptr_t match = 0;
    if (!FindUniquePattern(module, name, signature, &match, log))
        return false;

    uintptr_t callAddress = match + callOffset;
    if (!IsExecutableRange(module, callAddress, 5))
    {
        LogFormat(log, "%s callsite is outside executable sections", name);
        return false;
    }

    auto* call = reinterpret_cast<const uint8_t*>(callAddress);
    if (call[0] != 0xE8)
    {
        LogFormat(log, "%s callsite does not start with call rel32", name);
        return false;
    }

    int32_t rel = 0;
    std::memcpy(&rel, call + 1, sizeof(rel));

    uintptr_t resolved = static_cast<uintptr_t>(
        static_cast<intptr_t>(callAddress + 5) + static_cast<intptr_t>(rel)
    );
    if (!IsExecutableRange(module, resolved, 1))
    {
        LogFormat(log, "%s resolved target is outside executable sections", name);
        return false;
    }

    *target = resolved;
    return true;
}

bool ResolvePatternOffsetTarget(
    const ModuleExecutableSections& module,
    const char* name,
    const char* signature,
    size_t hookOffset,
    uintptr_t* target,
    SignatureScannerLogFn log
)
{
    uintptr_t match = 0;
    if (!FindUniquePattern(module, name, signature, &match, log))
        return false;

    uintptr_t resolved = match + hookOffset;
    if (!IsExecutableRange(module, resolved, 1))
    {
        LogFormat(log, "%s hook target is outside executable sections", name);
        return false;
    }

    *target = resolved;
    return true;
}
