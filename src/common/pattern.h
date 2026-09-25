#pragma once

#include <cstdint>

namespace bvh
{
    // Scans the main module's .text for an IDA-style signature ("48 8B ?? 05").
    // Returns the address of the match only when it is unique; *matchCount receives the number of matches.
    uintptr_t FindPattern(const char* signature, int* matchCount = nullptr);

    // True when the bytes at address match the signature.
    bool BytesMatch(uintptr_t address, const char* signature);

    // Target of a RIP-relative operand: instruction + length + disp32 at instruction + dispOffset.
    uintptr_t ResolveRipTarget(uintptr_t instruction, int dispOffset, int instructionLength);

    uintptr_t MainModuleBase();
    uint32_t MainModuleTimestamp();

    // Tests only: scan a separately mapped image instead of the host process.
    void SetModuleOverride(uintptr_t base);
}
