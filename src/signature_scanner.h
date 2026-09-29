#pragma once

#include <cstddef>
#include <cstdint>

static constexpr size_t MAX_PATTERN_BYTES = 256;
static constexpr size_t MAX_EXECUTABLE_SECTIONS = 32;

using SignatureScannerLogFn = void (*)(const char* message);

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

bool CompilePattern(
    const char* text,
    CompiledPattern* pattern,
    SignatureScannerLogFn log = nullptr
);
bool GetMainModuleExecutableSections(
    ModuleExecutableSections* module,
    SignatureScannerLogFn log = nullptr
);
bool IsExecutableRange(
    const ModuleExecutableSections& module,
    uintptr_t address,
    size_t size
);
bool PatternMatches(const uint8_t* cursor, const CompiledPattern& pattern);
bool FindUniquePattern(
    const ModuleExecutableSections& module,
    const char* name,
    const char* signature,
    uintptr_t* match,
    SignatureScannerLogFn log = nullptr
);
bool ResolveCallRel32Target(
    const ModuleExecutableSections& module,
    const char* name,
    const char* signature,
    size_t callOffset,
    uintptr_t* target,
    SignatureScannerLogFn log = nullptr
);
bool ResolvePatternOffsetTarget(
    const ModuleExecutableSections& module,
    const char* name,
    const char* signature,
    size_t hookOffset,
    uintptr_t* target,
    SignatureScannerLogFn log = nullptr
);
