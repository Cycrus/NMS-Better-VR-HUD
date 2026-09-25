// Better VR HUD for No Man's Sky: keeps the HUD in front of your eyes instead of in front of your body.
//
// The game places the main HUD panel relative to an anchor that follows your position but not your
// head rotation. This plugin replaces the panel's matrix at the moment the game sets it, so the layout
// you see when looking straight ahead turns with your head. Menus and other panels are left alone.
//
// Files next to the .asi:
//   BetterVrHud.ini  settings (created with defaults on first run)
//   BetterVrHud.log  startup diagnostics and lock status changes

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cstdint>
#include <cwchar>
#include <cwctype>

#include "common/hooks.h"
#include "common/hud_lock.h"
#include "common/log.h"
#include "common/nms.h"
#include "common/pattern.h"

using namespace bvh;

namespace
{
    constexpr const char* kVersion = "0.1.0";
    constexpr const wchar_t* kSection = L"BetterVrHud";
    constexpr char kDefaultIni[] =
        "; Better VR HUD settings\r\n"
        "[BetterVrHud]\r\n"
        "; 1 keeps the HUD in front of your eyes, 0 leaves the game default.\r\n"
        "HeadLock=1\r\n"
        "; Key that switches HeadLock in game: F1-F24, a virtual-key code such as 0x77, or 0 for none.\r\n"
        "ToggleKey=F8\r\n";
    constexpr int kResolveAttempts = 60;
    constexpr DWORD kResolveRetryMs = 500;

    struct Settings
    {
        bool headLock = true;
        int toggleKey = VK_F8;
    };

    Settings g_settings;
    nms::Addresses g_addr;
    nms::ApplyMatrixFn g_originalApplyMatrix = nullptr;
    nms::HudBuilderFn g_originalHudBuilder = nullptr;

    volatile LONG g_headLock = 1;
    bool g_toggleWasDown = false;
    LockStatus g_lastStatus = LockStatus::Ok;
    bool g_statusReported = false;

    // Caller of the HUD builder currently running on this thread (its ApplyMatrix call is nested inside).
    thread_local uintptr_t t_builderCaller = 0;

    void IniPath(HMODULE module, wchar_t* path)
    {
        DWORD length = GetModuleFileNameW(module, path, MAX_PATH);
        wchar_t* dot = length ? std::wcsrchr(path, L'.') : nullptr;
        if (dot)
            std::wcscpy(dot, L".ini");
    }

    // "F8", "0x77" or "119"; 0 disables the key.
    int ParseKey(const wchar_t* text)
    {
        if ((text[0] == L'F' || text[0] == L'f') && std::iswdigit(text[1]))
        {
            long number = std::wcstol(text + 1, nullptr, 10);
            return number >= 1 && number <= 24 ? VK_F1 + static_cast<int>(number) - 1 : 0;
        }
        return static_cast<int>(std::wcstol(text, nullptr, 0));
    }

    void LoadSettings(HMODULE module)
    {
        wchar_t path[MAX_PATH] = L"";
        IniPath(module, path);

        HANDLE file = CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file != INVALID_HANDLE_VALUE)
        {
            DWORD written = 0;
            WriteFile(file, kDefaultIni, static_cast<DWORD>(sizeof(kDefaultIni) - 1), &written, nullptr);
            CloseHandle(file);
        }

        g_settings.headLock = GetPrivateProfileIntW(kSection, L"HeadLock", 1, path) != 0;

        wchar_t key[32] = L"";
        GetPrivateProfileStringW(kSection, L"ToggleKey", L"F8", key, 32, path);
        g_settings.toggleKey = ParseKey(key);
        if (g_settings.toggleKey < 0 || g_settings.toggleKey > 0xFE)
            g_settings.toggleKey = 0;

        Logf("settings: HeadLock=%d ToggleKey=0x%02X\n", g_settings.headLock ? 1 : 0, g_settings.toggleKey);
    }

    void PollToggleKey()
    {
        if (!g_settings.toggleKey)
            return;

        bool down = (GetAsyncKeyState(g_settings.toggleKey) & 0x8000) != 0;
        if (down && !g_toggleWasDown)
        {
            LONG enabled = !g_headLock;
            InterlockedExchange(&g_headLock, enabled);
            Logf("head lock %s\n", enabled ? "on" : "off");
            LogFlush();
        }
        g_toggleWasDown = down;
    }

    void ReportStatus(LockStatus status)
    {
        if (g_statusReported && status == g_lastStatus)
            return;

        g_statusReported = true;
        g_lastStatus = status;
        Logf("[%.1f s] lock status: %s\n", static_cast<double>(NowMicros()) / 1e6, LockStatusName(status));
        LogFlush();
    }

    void HookApplyMatrix(uint32_t handle, const float* matrix)
    {
        auto returnAddress = reinterpret_cast<uintptr_t>(__builtin_return_address(0));
        if (returnAddress != g_addr.hudCallReturn || !g_headLock || !matrix ||
            t_builderCaller != g_addr.mainHudCallerReturn)
        {
            g_originalApplyMatrix(handle, matrix);
            return;
        }

        float locked[16];
        LockStatus status = ComputeHeadLockedLocal(g_addr, handle, matrix, locked);
        ReportStatus(status);
        g_originalApplyMatrix(handle, status == LockStatus::Ok ? locked : matrix);
    }

    void HookHudBuilder(void* object, float a, float b, float c)
    {
        auto caller = reinterpret_cast<uintptr_t>(__builtin_return_address(0));

        // The main HUD is rebuilt once per frame, which makes it a convenient place to poll the key.
        if (caller == g_addr.mainHudCallerReturn)
            PollToggleKey();

        uintptr_t outerCaller = t_builderCaller;
        t_builderCaller = caller;
        g_originalHudBuilder(object, a, b, c);
        t_builderCaller = outerCaller;
    }

    DWORD WINAPI InitThread(LPVOID)
    {
        bool resolved = false;
        for (int attempt = 0; attempt < kResolveAttempts && !resolved; ++attempt)
        {
            resolved = nms::ResolveCore(g_addr, attempt == kResolveAttempts - 1);
            if (!resolved)
                Sleep(kResolveRetryMs);
        }

        if (!resolved)
        {
            Logf("game code not recognised (NMS.exe build 0x%08X); HUD left unchanged\n", MainModuleTimestamp());
            LogFlush();
            return 0;
        }

        const nms::Addresses& a = g_addr;
        Logf("NMS.exe build 0x%08X: applyMatrix=+0x%llx hudBuilder=+0x%llx hudCall=+0x%llx mainHud=+0x%llx "
             "scene=+0x%llx view=+0x%llx\n",
            a.timestamp, a.applyMatrix - a.base, a.hudBuilder - a.base, a.hudCallReturn - a.base,
            a.mainHudCallerReturn - a.base, a.sceneManagerGlobal - a.base, a.viewObjectGlobal - a.base);

        if (hooks::Init())
        {
            hooks::Create(reinterpret_cast<void*>(a.hudBuilder), reinterpret_cast<void*>(&HookHudBuilder),
                reinterpret_cast<void**>(&g_originalHudBuilder), "HudBuilder");
            hooks::Create(reinterpret_cast<void*>(a.applyMatrix), reinterpret_cast<void*>(&HookApplyMatrix),
                reinterpret_cast<void**>(&g_originalApplyMatrix), "ApplyMatrix");
        }

        LogFlush();
        return 0;
    }
}

BOOL WINAPI DllMain(HINSTANCE module, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        DisableThreadLibraryCalls(module);
        NowMicros();

        LogOpen(module, L"BetterVrHud", L"log", false);
        Logf("Better VR HUD %s\n", kVersion);
        LoadSettings(module);
        g_headLock = g_settings.headLock ? 1 : 0;

        HANDLE thread = CreateThread(nullptr, 0, InitThread, nullptr, 0, nullptr);
        if (thread)
            CloseHandle(thread);
    }
    else if (reason == DLL_PROCESS_DETACH)
    {
        LogFlush();
    }

    return TRUE;
}
