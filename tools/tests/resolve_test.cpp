// Maps NMS.exe as an image (no code runs) and checks that every address resolves to the expected RVA.
//   resolve_test.exe "C:\...\No Man's Sky\Binaries\NMS.exe"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cstdio>

#include "common/log.h"
#include "common/nms.h"
#include "common/pattern.h"

namespace
{
    struct Expected
    {
        const char* name;
        uintptr_t bvh::nms::Addresses::*field;
        uintptr_t rva;
    };

    // RVAs for build 0x6AB0FFC9, cross-checked against the Cheat Engine / radare2 notes in the repo.
    const Expected kExpected[] = {
        { "applyMatrix", &bvh::nms::Addresses::applyMatrix, 0x1838500 },
        { "hudCallReturn", &bvh::nms::Addresses::hudCallReturn, 0xAB1CA2 },
        { "hudBuilder", &bvh::nms::Addresses::hudBuilder, 0xAB1A20 },
        { "mainHudCallerReturn", &bvh::nms::Addresses::mainHudCallerReturn, 0x932D08 },
        { "vrUpdate", &bvh::nms::Addresses::vrUpdate, 0x2C356C0 },
        { "sceneManagerGlobal", &bvh::nms::Addresses::sceneManagerGlobal, 0x6E0D098 },
        { "viewObjectGlobal", &bvh::nms::Addresses::viewObjectGlobal, 0x6E7AAC0 },
        { "hudVecA", &bvh::nms::Addresses::hudVecA, 0x70F8D40 },
        { "hudVecB", &bvh::nms::Addresses::hudVecB, 0x70F94D0 },
        { "hudVecC", &bvh::nms::Addresses::hudVecC, 0x70F9090 },
        { "cameraCopy", &bvh::nms::Addresses::cameraCopy, 0x6EB4B00 },
        { "cameraMatrix", &bvh::nms::Addresses::cameraMatrix, 0x6E7CA30 },
    };
}

int wmain(int argc, wchar_t** argv)
{
    if (argc < 2)
        return 2;

    HMODULE image = LoadLibraryExW(argv[1], nullptr, DONT_RESOLVE_DLL_REFERENCES);
    if (!image)
    {
        std::printf("map failed: %lu\n", GetLastError());
        return 1;
    }

    auto base = reinterpret_cast<uintptr_t>(image);
    bvh::SetModuleOverride(base);
    bvh::LogOpen(nullptr, L"resolve_test", L"log", false);

    bvh::nms::Addresses addresses;
    if (!bvh::nms::Resolve(addresses, true))
    {
        bvh::LogFlush();
        std::printf("resolve failed (see resolve_test.log)\n");
        return 1;
    }

    int failures = 0;
    for (const Expected& e : kExpected)
    {
        uintptr_t rva = addresses.*e.field - base;
        bool ok = rva == e.rva;
        failures += ok ? 0 : 1;
        std::printf("%-20s 0x%08llx %s\n", e.name, static_cast<unsigned long long>(rva), ok ? "ok" : "MISMATCH");
    }

    std::printf("build 0x%08X, %d failure(s)\n", addresses.timestamp, failures);
    return failures == 0 ? 0 : 1;
}
