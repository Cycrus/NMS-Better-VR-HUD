#include "signature_scanner.h"

#include <cstdio>

static bool CompilePatternAcceptsHexBytesAndWildcards()
{
    CompiledPattern pattern = {};
    if (!CompilePattern("48 8D ?? 20", &pattern))
        return false;

    if (pattern.length != 4)
        return false;

    if (pattern.bytes[0].wildcard || pattern.bytes[0].value != 0x48)
        return false;

    if (pattern.bytes[1].wildcard || pattern.bytes[1].value != 0x8D)
        return false;

    if (!pattern.bytes[2].wildcard)
        return false;

    if (pattern.bytes[3].wildcard || pattern.bytes[3].value != 0x20)
        return false;

    const uint8_t matchingBytes[4] = {0x48, 0x8D, 0xFF, 0x20};
    return PatternMatches(matchingBytes, pattern);
}

static bool CompilePatternRejectsInvalidToken()
{
    CompiledPattern pattern = {};
    return !CompilePattern("48 ZZ", &pattern);
}

int main()
{
    if (!CompilePatternAcceptsHexBytesAndWildcards())
    {
        std::printf("CompilePatternAcceptsHexBytesAndWildcards failed\n");
        return 1;
    }

    if (!CompilePatternRejectsInvalidToken())
    {
        std::printf("CompilePatternRejectsInvalidToken failed\n");
        return 1;
    }

    return 0;
}
