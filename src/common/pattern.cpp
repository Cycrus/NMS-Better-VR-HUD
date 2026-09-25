#include "common/pattern.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cstdlib>
#include <cstring>

namespace bvh
{
    namespace
    {
        constexpr int kMaxPatternLength = 256;

        // Parsed signature: value 0-255, or -1 for a wildcard.
        int ParseSignature(const char* signature, int* bytes)
        {
            int count = 0;
            const char* p = signature;

            while (*p && count < kMaxPatternLength)
            {
                while (*p == ' ')
                    ++p;
                if (!*p)
                    break;

                if (*p == '?')
                {
                    bytes[count++] = -1;
                    while (*p == '?')
                        ++p;
                    continue;
                }

                char hex[3] = { p[0], p[1], 0 };
                bytes[count++] = static_cast<int>(std::strtoul(hex, nullptr, 16));
                p += 2;
            }

            return count;
        }

        bool TextSection(uintptr_t& begin, uintptr_t& end)
        {
            uintptr_t base = MainModuleBase();
            auto dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
            auto nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
            auto section = IMAGE_FIRST_SECTION(nt);

            for (WORD i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++section)
            {
                if (std::memcmp(section->Name, ".text", 5) == 0)
                {
                    begin = base + section->VirtualAddress;
                    end = begin + section->Misc.VirtualSize;
                    return true;
                }
            }

            return false;
        }

        bool MatchAt(const uint8_t* at, const int* bytes, int length)
        {
            for (int i = 0; i < length; ++i)
            {
                if (bytes[i] >= 0 && at[i] != bytes[i])
                    return false;
            }
            return true;
        }
    }

    namespace
    {
        uintptr_t g_moduleOverride = 0;
    }

    void SetModuleOverride(uintptr_t base)
    {
        g_moduleOverride = base;
    }

    uintptr_t MainModuleBase()
    {
        if (g_moduleOverride)
            return g_moduleOverride;
        return reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    }

    uint32_t MainModuleTimestamp()
    {
        uintptr_t base = MainModuleBase();
        auto dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
        auto nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
        return nt->FileHeader.TimeDateStamp;
    }

    uintptr_t FindPattern(const char* signature, int* matchCount)
    {
        int bytes[kMaxPatternLength];
        int length = ParseSignature(signature, bytes);

        uintptr_t begin = 0;
        uintptr_t end = 0;
        if (length == 0 || !TextSection(begin, end))
        {
            if (matchCount)
                *matchCount = 0;
            return 0;
        }

        int count = 0;
        uintptr_t found = 0;
        const uint8_t* p = reinterpret_cast<const uint8_t*>(begin);
        const uint8_t* last = reinterpret_cast<const uint8_t*>(end) - length;

        // Signatures in this project never start with a wildcard; memchr on the first byte keeps the scan fast.
        while (p <= last)
        {
            p = static_cast<const uint8_t*>(std::memchr(p, bytes[0], static_cast<size_t>(last - p) + 1));
            if (!p)
                break;

            if (MatchAt(p, bytes, length))
            {
                ++count;
                found = reinterpret_cast<uintptr_t>(p);
            }
            ++p;
        }

        if (matchCount)
            *matchCount = count;

        return count == 1 ? found : 0;
    }

    bool BytesMatch(uintptr_t address, const char* signature)
    {
        int bytes[kMaxPatternLength];
        int length = ParseSignature(signature, bytes);
        return length > 0 && MatchAt(reinterpret_cast<const uint8_t*>(address), bytes, length);
    }

    uintptr_t ResolveRipTarget(uintptr_t instruction, int dispOffset, int instructionLength)
    {
        int32_t displacement = 0;
        std::memcpy(&displacement, reinterpret_cast<const void*>(instruction + dispOffset), sizeof(displacement));
        return instruction + instructionLength + displacement;
    }
}
