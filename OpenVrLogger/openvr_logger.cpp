#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "openvr.h"

using VR_GetGenericInterfaceFn = void* (*)(const char* interfaceName, int* error);

using WaitGetPosesFn = vr::EVRCompositorError(__fastcall*)(
    void* self,
    vr::TrackedDevicePose_t* renderPoses,
    uint32_t renderPoseCount,
    vr::TrackedDevicePose_t* gamePoses,
    uint32_t gamePoseCount
);

static VR_GetGenericInterfaceFn g_originalVR_GetGenericInterface = nullptr;
static WaitGetPosesFn g_originalWaitGetPoses = nullptr;
static void** g_waitGetPosesSlot = nullptr;
static volatile LONG g_waitGetPosesHooked = 0;
static volatile LONG g_poseLogCount = 0;

static CRITICAL_SECTION g_logLock;
static bool g_logLockReady = false;

static void LogLine(const char* text)
{
    if (g_logLockReady)
        EnterCriticalSection(&g_logLock);

    FILE* file = nullptr;
    fopen_s(&file, "OpenVrLogger.txt", "a");

    if (file)
    {
        SYSTEMTIME now = {};
        GetLocalTime(&now);
        std::fprintf(
            file,
            "%02u:%02u:%02u.%03u %s",
            now.wHour,
            now.wMinute,
            now.wSecond,
            now.wMilliseconds,
            text
        );
        fclose(file);
    }

    if (g_logLockReady)
        LeaveCriticalSection(&g_logLock);
}

static void LogFormat(const char* format, ...)
{
    char buffer[1024];

    va_list args;
    va_start(args, format);
    std::vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);

    LogLine(buffer);
}

static bool IsReadablePointer(const void* pointer)
{
    if (!pointer)
        return false;

    MEMORY_BASIC_INFORMATION info = {};
    if (!VirtualQuery(pointer, &info, sizeof(info)))
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

static void LogPoseSample(
    const vr::TrackedDevicePose_t* renderPoses,
    uint32_t renderPoseCount,
    const vr::TrackedDevicePose_t* gamePoses,
    uint32_t gamePoseCount,
    vr::EVRCompositorError result
)
{
    LONG count = InterlockedIncrement(&g_poseLogCount);

    if (count > 240 && (count % 300) != 0)
        return;

    const vr::TrackedDevicePose_t* hmd = nullptr;
    const char* source = "none";

    if (renderPoses && renderPoseCount > 0)
    {
        hmd = &renderPoses[0];
        source = "render";
    }
    else if (gamePoses && gamePoseCount > 0)
    {
        hmd = &gamePoses[0];
        source = "game";
    }

    if (!hmd)
    {
        LogFormat(
            "WaitGetPoses #%ld result=%d render=%p/%u game=%p/%u no pose arrays\n",
            count,
            static_cast<int>(result),
            renderPoses,
            renderPoseCount,
            gamePoses,
            gamePoseCount
        );
        return;
    }

    const vr::HmdMatrix34_t& m = hmd->mDeviceToAbsoluteTracking;

    LogFormat(
        "WaitGetPoses #%ld result=%d source=%s render=%p/%u game=%p/%u valid=%d connected=%d tracking=%d matrix=[%.6f %.6f %.6f %.6f | %.6f %.6f %.6f %.6f | %.6f %.6f %.6f %.6f]\n",
        count,
        static_cast<int>(result),
        source,
        renderPoses,
        renderPoseCount,
        gamePoses,
        gamePoseCount,
        hmd->bPoseIsValid ? 1 : 0,
        hmd->bDeviceIsConnected ? 1 : 0,
        static_cast<int>(hmd->eTrackingResult),
        m.m[0][0], m.m[0][1], m.m[0][2], m.m[0][3],
        m.m[1][0], m.m[1][1], m.m[1][2], m.m[1][3],
        m.m[2][0], m.m[2][1], m.m[2][2], m.m[2][3]
    );
}

static vr::EVRCompositorError __fastcall HookWaitGetPoses(
    void* self,
    vr::TrackedDevicePose_t* renderPoses,
    uint32_t renderPoseCount,
    vr::TrackedDevicePose_t* gamePoses,
    uint32_t gamePoseCount
)
{
    vr::EVRCompositorError result = g_originalWaitGetPoses(
        self,
        renderPoses,
        renderPoseCount,
        gamePoses,
        gamePoseCount
    );

    LogPoseSample(renderPoses, renderPoseCount, gamePoses, gamePoseCount, result);
    return result;
}

static void TryHookWaitGetPoses(void* compositor)
{
    if (!compositor)
        return;

    if (InterlockedCompareExchange(&g_waitGetPosesHooked, 1, 0) != 0)
        return;

    if (!IsReadablePointer(compositor))
    {
        LogFormat("IVRCompositor pointer is not readable: %p\n", compositor);
        InterlockedExchange(&g_waitGetPosesHooked, 0);
        return;
    }

    void** vtable = *reinterpret_cast<void***>(compositor);

    if (!IsReadablePointer(vtable))
    {
        LogFormat("IVRCompositor vtable is not readable: compositor=%p vtable=%p\n", compositor, vtable);
        InterlockedExchange(&g_waitGetPosesHooked, 0);
        return;
    }

    g_waitGetPosesSlot = &vtable[2];
    g_originalWaitGetPoses = reinterpret_cast<WaitGetPosesFn>(*g_waitGetPosesSlot);

    LogFormat(
        "IVRCompositor=%p vtable=%p WaitGetPosesSlot=%p original=%p\n",
        compositor,
        vtable,
        g_waitGetPosesSlot,
        reinterpret_cast<void*>(g_originalWaitGetPoses)
    );

    DWORD oldProtect = 0;
    if (!VirtualProtect(g_waitGetPosesSlot, sizeof(void*), PAGE_EXECUTE_READWRITE, &oldProtect))
    {
        LogFormat("VirtualProtect failed for WaitGetPosesSlot=%p error=%lu\n", g_waitGetPosesSlot, GetLastError());
        InterlockedExchange(&g_waitGetPosesHooked, 0);
        return;
    }

    *g_waitGetPosesSlot = reinterpret_cast<void*>(&HookWaitGetPoses);

    DWORD ignored = 0;
    VirtualProtect(g_waitGetPosesSlot, sizeof(void*), oldProtect, &ignored);
    FlushInstructionCache(GetCurrentProcess(), g_waitGetPosesSlot, sizeof(void*));

    LogLine("Hooked IVRCompositor::WaitGetPoses.\n");
}

static void* HookVR_GetGenericInterface(const char* interfaceName, int* error)
{
    void* result = g_originalVR_GetGenericInterface(interfaceName, error);

    int errorValue = error ? *error : -1;

    LogFormat(
        "VR_GetGenericInterface name=%s result=%p error=%d\n",
        interfaceName ? interfaceName : "<null>",
        result,
        errorValue
    );

    if (interfaceName && result && std::strcmp(interfaceName, "IVRCompositor_029") == 0)
        TryHookWaitGetPoses(result);

    return result;
}

static bool PatchImport(
    HMODULE module,
    const char* importedModuleName,
    const char* importedFunctionName,
    void* replacement,
    void** original
)
{
    auto base = reinterpret_cast<uint8_t*>(module);
    auto dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);

    if (dos->e_magic != IMAGE_DOS_SIGNATURE)
        return false;

    auto nt = reinterpret_cast<IMAGE_NT_HEADERS*>(base + dos->e_lfanew);

    if (nt->Signature != IMAGE_NT_SIGNATURE)
        return false;

    IMAGE_DATA_DIRECTORY importsDirectory =
        nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];

    if (!importsDirectory.VirtualAddress)
        return false;

    auto descriptor = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(
        base + importsDirectory.VirtualAddress
    );

    for (; descriptor->Name; ++descriptor)
    {
        const char* moduleName = reinterpret_cast<const char*>(base + descriptor->Name);

        if (_stricmp(moduleName, importedModuleName) != 0)
            continue;

        auto thunk = reinterpret_cast<IMAGE_THUNK_DATA*>(base + descriptor->FirstThunk);
        auto originalThunk = reinterpret_cast<IMAGE_THUNK_DATA*>(base + descriptor->OriginalFirstThunk);

        for (; originalThunk->u1.AddressOfData; ++thunk, ++originalThunk)
        {
            if (IMAGE_SNAP_BY_ORDINAL(originalThunk->u1.Ordinal))
                continue;

            auto importByName = reinterpret_cast<IMAGE_IMPORT_BY_NAME*>(
                base + originalThunk->u1.AddressOfData
            );

            if (std::strcmp(reinterpret_cast<const char*>(importByName->Name), importedFunctionName) != 0)
                continue;

            auto slot = reinterpret_cast<void**>(&thunk->u1.Function);
            *original = *slot;

            DWORD oldProtect = 0;
            if (!VirtualProtect(slot, sizeof(void*), PAGE_READWRITE, &oldProtect))
                return false;

            *slot = replacement;

            DWORD ignored = 0;
            VirtualProtect(slot, sizeof(void*), oldProtect, &ignored);
            FlushInstructionCache(GetCurrentProcess(), slot, sizeof(void*));

            LogFormat(
                "Patched IAT %s!%s slot=%p original=%p replacement=%p\n",
                importedModuleName,
                importedFunctionName,
                slot,
                *original,
                replacement
            );

            return true;
        }
    }

    return false;
}

static DWORD WINAPI WorkerThread(LPVOID)
{
    LogLine("OpenVrLogger loaded.\n");

    HMODULE nms = GetModuleHandleW(nullptr);

    LogFormat("Main module base=%p\n", nms);

    for (int attempt = 0; attempt < 300; ++attempt)
    {
        if (PatchImport(
            nms,
            "openvr_api.dll",
            "VR_GetGenericInterface",
            reinterpret_cast<void*>(&HookVR_GetGenericInterface),
            reinterpret_cast<void**>(&g_originalVR_GetGenericInterface)
        ))
        {
            return 0;
        }

        Sleep(100);
    }

    LogLine("Failed to patch VR_GetGenericInterface import.\n");
    return 0;
}

BOOL WINAPI DllMain(HINSTANCE module, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        DisableThreadLibraryCalls(module);
        InitializeCriticalSection(&g_logLock);
        g_logLockReady = true;

        HANDLE thread = CreateThread(nullptr, 0, WorkerThread, nullptr, 0, nullptr);

        if (thread)
            CloseHandle(thread);
    }

    return TRUE;
}
