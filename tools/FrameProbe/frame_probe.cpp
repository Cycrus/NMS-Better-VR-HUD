// FrameProbe: diagnostic ASI for head-locking the VR HUD.
//
// Writes FrameProbe_<timestamp>.csv next to the .asi with, per frame:
//   - the matrix the game passes for each HUD node (the HUD builder's ApplyMatrix call site),
//     the head-locked replacement and the lock status,
//   - each HUD node's scene-graph state (local, world, anchor),
//   - the game camera matrix and the raw OpenVR HMD pose,
// plus event ordering (thread id, microsecond timestamps, scene frame counter).
//
// Keys (polled every frame, work without window focus):
//   F8  toggle mode: off <-> lock main HUD  (beeps mode+1 times)
//   F9  write a MARK row (one low beep)

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "common/hooks.h"
#include "common/hud_lock.h"
#include "common/log.h"
#include "common/nms.h"
#include "common/openvr_hook.h"

using namespace bvh;

namespace
{
    constexpr const char* kProbeVersion = "FrameProbe 3";

    // Locking every HUD-builder node also pulls the game menu panel into your face, so only the main HUD.
    enum Mode : LONG { ModeOff, ModeLockMain, ModeCount };
    const char* const kModeNames[ModeCount] = { "off", "lock_main" };

    constexpr int kMaxTracked = 8;
    constexpr int64_t kTrackedTimeoutUs = 2000000;
    constexpr int64_t kChainDumpIntervalUs = 5000000;
    constexpr int64_t kFlushIntervalUs = 250000;
    constexpr int64_t kCompositorProbeIntervalUs = 1000000;
    constexpr int kMaxBuilderRowsPerFrame = 64;
    constexpr int kChainMaxDepth = 16;
    constexpr size_t kNodeDumpBytes = 0x100;

    struct TrackedHud
    {
        uint32_t handle = 0;
        uintptr_t builderCaller = 0;
        bool chainDumped = false;
        int64_t lastSeenUs = 0;
        int64_t lastChainDumpUs = 0;
    };

    nms::Addresses g_addr;
    nms::ApplyMatrixFn g_originalApplyMatrix = nullptr;
    nms::HudBuilderFn g_originalHudBuilder = nullptr;
    nms::VrUpdateFn g_originalVrUpdate = nullptr;

    CRITICAL_SECTION g_stateLock;
    TrackedHud g_tracked[kMaxTracked];
    int g_trackedCount = 0;

    volatile LONG g_mode = ModeOff;
    int64_t g_lastFlushUs = 0;
    int64_t g_lastCompositorProbeUs = 0;
    bool g_keyWasDown[256] = {};
    int g_markerCount = 0;
    int32_t g_builderRowsFrame = -1;
    int g_builderRows = 0;

    // Caller of the HUD builder currently running on this thread (its ApplyMatrix call is nested inside).
    thread_local uintptr_t t_builderCaller = 0;

    // One CSV row: TAG,t_us,thread,scene_frame,...
    class Row
    {
    public:
        Row(const char* tag, int32_t frame)
        {
            Append("%s,%lld,%lu,%d", tag, static_cast<long long>(NowMicros()), GetCurrentThreadId(), frame);
        }

        Row& Int(long long value) { return Append(",%lld", value); }
        Row& Hex(unsigned long long value) { return Append(",0x%llx", value); }
        Row& Float(float value) { return Append(",%.9g", value); }
        Row& Str(const char* value) { return Append(",%s", value); }

        Row& Floats(const float* values, int count)
        {
            for (int i = 0; i < count; ++i)
            {
                if (values)
                    Float(values[i]);
                else
                    Append(",nan");
            }
            return *this;
        }

        void Emit()
        {
            buffer_[length_++] = '\n';
            LogWrite(buffer_, static_cast<size_t>(length_));
        }

    private:
        __attribute__((format(__MINGW_PRINTF_FORMAT, 2, 3)))
        Row& Append(const char* format, ...)
        {
            int room = static_cast<int>(sizeof(buffer_)) - length_ - 1;
            if (room <= 0)
                return *this;

            va_list args;
            va_start(args, format);
            int written = std::vsnprintf(buffer_ + length_, static_cast<size_t>(room), format, args);
            va_end(args);

            if (written > 0)
                length_ += written < room ? written : room - 1;
            return *this;
        }

        char buffer_[8192];
        int length_ = 0;
    };

    bool IsReadable(const void* address, size_t size)
    {
        MEMORY_BASIC_INFORMATION info = {};
        if (!address || !VirtualQuery(address, &info, sizeof(info)))
            return false;
        if (info.State != MEM_COMMIT || (info.Protect & (PAGE_NOACCESS | PAGE_GUARD)))
            return false;

        auto begin = reinterpret_cast<uintptr_t>(address);
        auto regionEnd = reinterpret_cast<uintptr_t>(info.BaseAddress) + info.RegionSize;
        return begin + size <= regionEnd;
    }

    int32_t CurrentFrame()
    {
        nms::Scene scene(g_addr.sceneManagerGlobal);
        return scene.Refresh() ? scene.Frame() : -1;
    }

    const float* Camera()
    {
        return g_addr.viewObjectGlobal ? nms::CameraMatrix(g_addr) : nullptr;
    }

    DWORD WINAPI BeepThread(LPVOID parameter)
    {
        auto packed = reinterpret_cast<uintptr_t>(parameter);
        DWORD count = static_cast<DWORD>(packed >> 16);
        DWORD frequency = static_cast<DWORD>(packed & 0xFFFF);

        for (DWORD i = 0; i < count; ++i)
        {
            Beep(frequency, 110);
            Sleep(70);
        }
        return 0;
    }

    void BeepAsync(int count, int frequency)
    {
        auto packed = (static_cast<uintptr_t>(count) << 16) | static_cast<uintptr_t>(frequency);
        HANDLE thread = CreateThread(nullptr, 0, BeepThread, reinterpret_cast<LPVOID>(packed), 0, nullptr);
        if (thread)
            CloseHandle(thread);
    }

    // Caller holds g_stateLock.
    TrackedHud& Track(uint32_t handle)
    {
        for (int i = 0; i < g_trackedCount; ++i)
        {
            if (g_tracked[i].handle == handle)
                return g_tracked[i];
        }

        int index = g_trackedCount;
        if (g_trackedCount < kMaxTracked)
        {
            ++g_trackedCount;
        }
        else
        {
            index = 0;
            for (int i = 1; i < kMaxTracked; ++i)
            {
                if (g_tracked[i].lastSeenUs < g_tracked[index].lastSeenUs)
                    index = i;
            }
        }

        g_tracked[index] = TrackedHud();
        g_tracked[index].handle = handle;
        Row("TRACK", CurrentFrame()).Hex(handle).Int(index).Emit();
        return g_tracked[index];
    }

    // Anchor (parent) and origin (grandparent) slots of a HUD node, -1 when missing.
    void AnchorSlots(const nms::Scene& scene, int slot, int& anchorSlot, int& originSlot)
    {
        anchorSlot = slot >= 0 ? scene.SlotOf(scene.ParentHandle(slot)) : -1;
        originSlot = anchorSlot >= 0 ? scene.SlotOf(scene.ParentHandle(anchorSlot)) : -1;
    }

    void Snapshot(const TrackedHud& hud, int32_t frame, LONG mode)
    {
        nms::Scene scene(g_addr.sceneManagerGlobal);
        bool ready = scene.Refresh();
        int slot = ready ? scene.SlotOf(hud.handle) : -1;
        int anchorSlot = -1;
        int originSlot = -1;
        AnchorSlots(scene, slot, anchorSlot, originSlot);

        openvr::PoseSample pose;
        bool havePose = openvr::Latest(pose);

        Row row("SNAP", frame);
        row.Int(mode).Hex(hud.handle).Int(slot).Hex(anchorSlot >= 0 ? scene.ParentHandle(slot) : 0).Int(anchorSlot);
        row.Int(slot >= 0 ? scene.Type(slot) : -1);
        row.Floats(slot >= 0 ? scene.Local(slot) : nullptr, 16);
        row.Floats(slot >= 0 ? scene.World(slot) : nullptr, 16);
        row.Floats(anchorSlot >= 0 ? scene.World(anchorSlot) : nullptr, 16);
        row.Floats(Camera(), 16);
        row.Int(havePose ? static_cast<long long>(pose.sequence) : -1);
        row.Int(havePose ? NowMicros() - pose.timeMicros : -1);
        row.Int(havePose && pose.valid);
        row.Floats(havePose ? &pose.m[0][0] : nullptr, 12);
        row.Floats(anchorSlot >= 0 ? scene.Local(anchorSlot) : nullptr, 16);
        row.Floats(originSlot >= 0 ? scene.World(originSlot) + 12 : nullptr, 3);
        row.Emit();
    }

    void DumpNode(uint32_t hudHandle, uint32_t handle, const void* node, int32_t frame)
    {
        if (!IsReadable(node, kNodeDumpBytes))
            return;

        char hex[kNodeDumpBytes * 2 + 1];
        auto bytes = static_cast<const uint8_t*>(node);
        for (size_t i = 0; i < kNodeDumpBytes; ++i)
            std::snprintf(hex + i * 2, 3, "%02x", bytes[i]);

        Row("NODE", frame).Hex(hudHandle).Hex(handle).Hex(reinterpret_cast<uintptr_t>(node)).Str(hex).Emit();
    }

    void DumpChain(const TrackedHud& hud, int32_t frame, bool withNodes)
    {
        nms::Scene scene(g_addr.sceneManagerGlobal);
        if (!scene.Refresh())
            return;

        uint32_t handle = hud.handle;
        for (int depth = 0; depth < kChainMaxDepth; ++depth)
        {
            int slot = scene.SlotOf(handle);
            if (slot < 0)
                break;

            const void* node = scene.NodeObject(slot);
            Row row("CHAIN", frame);
            row.Hex(hud.handle).Int(depth).Hex(handle).Int(slot).Int(scene.Type(slot)).Hex(reinterpret_cast<uintptr_t>(node));
            row.Floats(scene.Local(slot), 16).Floats(scene.World(slot), 16);
            row.Emit();

            if (withNodes)
                DumpNode(hud.handle, handle, node, frame);

            handle = scene.ParentHandle(slot);
        }
    }

    bool KeyPressed(int key)
    {
        bool down = (GetAsyncKeyState(key) & 0x8000) != 0;
        bool pressed = down && !g_keyWasDown[key];
        g_keyWasDown[key] = down;
        return pressed;
    }

    void PollKeys(int32_t frame)
    {
        if (KeyPressed(VK_F8))
        {
            LONG mode = (g_mode + 1) % ModeCount;
            InterlockedExchange(&g_mode, mode);
            Row("MODE", frame).Int(mode).Str(kModeNames[mode]).Emit();
            BeepAsync(mode + 1, 880);
        }

        if (KeyPressed(VK_F9))
        {
            ++g_markerCount;
            Row("MARK", frame).Int(g_markerCount).Emit();
            BeepAsync(1, 440);
        }
    }

    void OnPose(const openvr::PoseSample& sample)
    {
        Row row("POSE", CurrentFrame());
        row.Int(static_cast<long long>(sample.sequence)).Int(sample.result).Int(sample.valid);
        row.Floats(&sample.m[0][0], 12).Floats(Camera(), 16);
        row.Emit();
    }

    void HookApplyMatrix(uint32_t handle, const float* matrix)
    {
        auto returnAddress = reinterpret_cast<uintptr_t>(__builtin_return_address(0));
        if (returnAddress != g_addr.hudCallReturn || !matrix)
        {
            g_originalApplyMatrix(handle, matrix);
            return;
        }

        uintptr_t caller = t_builderCaller;
        bool isMain = caller == g_addr.mainHudCallerReturn;

        EnterCriticalSection(&g_stateLock);
        TrackedHud& hud = Track(handle);
        hud.builderCaller = caller;
        hud.lastSeenUs = NowMicros();
        LeaveCriticalSection(&g_stateLock);

        LONG mode = g_mode;
        bool wantLock = mode == ModeLockMain && isMain;

        float replacement[16];
        LockStatus status = LockStatus::Ok;
        bool replaced = false;
        if (wantLock)
        {
            status = ComputeHeadLockedLocal(g_addr, handle, matrix, replacement);
            replaced = status == LockStatus::Ok;
        }

        nms::Scene scene(g_addr.sceneManagerGlobal);
        int slot = scene.Refresh() ? scene.SlotOf(handle) : -1;
        int anchorSlot = -1;
        int originSlot = -1;
        AnchorSlots(scene, slot, anchorSlot, originSlot);

        Row row("AM", scene.Frame());
        row.Hex(handle).Hex(caller ? caller - g_addr.base : 0).Int(isMain).Int(wantLock).Int(static_cast<int>(status)).Int(replaced);
        row.Floats(matrix, 16).Floats(replaced ? replacement : nullptr, 16).Floats(Camera(), 16);
        row.Floats(anchorSlot >= 0 ? scene.Local(anchorSlot) : nullptr, 16);
        row.Floats(anchorSlot >= 0 ? scene.World(anchorSlot) : nullptr, 16);
        row.Floats(originSlot >= 0 ? scene.World(originSlot) + 12 : nullptr, 3);
        row.Emit();

        g_originalApplyMatrix(handle, replaced ? replacement : matrix);
    }

    void HookHudBuilder(void* object, float a, float b, float c)
    {
        auto caller = reinterpret_cast<uintptr_t>(__builtin_return_address(0));
        int32_t frame = CurrentFrame();

        if (frame != g_builderRowsFrame)
        {
            g_builderRowsFrame = frame;
            g_builderRows = 0;
        }

        if (++g_builderRows <= kMaxBuilderRowsPerFrame)
        {
            auto bytes = static_cast<const uint8_t*>(object);
            uint32_t handle10 = 0;
            uint32_t handle14 = 0;
            if (IsReadable(bytes + 0x10, 8))
            {
                std::memcpy(&handle10, bytes + 0x10, 4);
                std::memcpy(&handle14, bytes + 0x14, 4);
            }

            auto viewObject = *reinterpret_cast<const uintptr_t*>(g_addr.viewObjectGlobal);

            Row row("HB", frame);
            row.Hex(caller - g_addr.base).Hex(reinterpret_cast<uintptr_t>(object)).Hex(handle10).Hex(handle14);
            row.Float(a).Float(b).Float(c);
            row.Floats(reinterpret_cast<const float*>(g_addr.hudVecA), 4);
            row.Floats(reinterpret_cast<const float*>(g_addr.hudVecB), 4);
            row.Floats(reinterpret_cast<const float*>(g_addr.hudVecC), 4);
            row.Hex(viewObject ? viewObject - g_addr.base : 0);
            row.Emit();
        }

        uintptr_t outerCaller = t_builderCaller;
        t_builderCaller = caller;
        g_originalHudBuilder(object, a, b, c);
        t_builderCaller = outerCaller;
    }

    void HookVrUpdate(void* self)
    {
        int32_t frame = CurrentFrame();
        Row("VU0", frame).Floats(Camera(), 16).Emit();

        g_originalVrUpdate(self);

        int64_t now = NowMicros();
        Row("VU1", frame).Floats(Camera(), 16).Emit();

        PollKeys(frame);

        if (!openvr::Hooked() && now - g_lastCompositorProbeUs > kCompositorProbeIntervalUs)
        {
            g_lastCompositorProbeUs = now;
            openvr::TryHookExistingCompositor();
        }

        LONG mode = g_mode;

        EnterCriticalSection(&g_stateLock);
        for (int i = 0; i < g_trackedCount; ++i)
        {
            TrackedHud& hud = g_tracked[i];
            if (now - hud.lastSeenUs > kTrackedTimeoutUs)
                continue;

            Snapshot(hud, frame, mode);

            if (!hud.chainDumped || now - hud.lastChainDumpUs > kChainDumpIntervalUs)
            {
                DumpChain(hud, frame, !hud.chainDumped);
                hud.chainDumped = true;
                hud.lastChainDumpUs = now;
            }
        }
        LeaveCriticalSection(&g_stateLock);

        if (now - g_lastFlushUs > kFlushIntervalUs)
        {
            g_lastFlushUs = now;
            LogFlush();
        }
    }

    void WriteHeader()
    {
        const nms::Addresses& a = g_addr;
        Logf("# %s  build=0x%08X  base=%p\n", kProbeVersion, a.timestamp, reinterpret_cast<void*>(a.base));
        Logf("# rva applyMatrix=0x%llx hudCallReturn=0x%llx hudBuilder=0x%llx mainHudCaller=0x%llx vrUpdate=0x%llx\n",
            a.applyMatrix - a.base, a.hudCallReturn - a.base, a.hudBuilder - a.base,
            a.mainHudCallerReturn - a.base, a.vrUpdate - a.base);
        Logf("# rva sceneManager=0x%llx viewObject=0x%llx hudVec=0x%llx,0x%llx,0x%llx cameraCopy=0x%llx\n",
            a.sceneManagerGlobal - a.base, a.viewObjectGlobal - a.base,
            a.hudVecA - a.base, a.hudVecB - a.base, a.hudVecC - a.base, a.cameraCopy - a.base);
        Logf("# every row: tag,t_us,thread,scene_frame,...\n");
        Logf("# SNAP  mode,handle,slot,anchor,anchor_slot,type,local[16],world[16],anchor_world[16],camera[16],"
             "pose_seq,pose_age_us,pose_valid,pose[12],anchor_local[16],origin[3]\n");
        Logf("# AM    handle,caller_rva,is_main,want_lock,status,replaced,game[16],replacement[16],camera[16],"
             "anchor_local[16],anchor_world[16],origin[3]\n");
        Logf("# HB    caller_rva,object,handle10,handle14,a,b,c,vecA[4],vecB[4],vecC[4],view_rva\n");
        Logf("# POSE  seq,result,valid,pose[12],camera[16]\n");
        Logf("# VU0/VU1 camera[16]\n");
        Logf("# CHAIN hud,depth,handle,slot,type,node,local[16],world[16]\n");
        Logf("# NODE  hud,handle,node,hex[256]\n");
        Logf("# MODE  mode,name | MARK n | TRACK handle,index | INFO text\n");
        Logf("# lock status: 0 ok, 1 no_camera, 2 no_scene, 3 bad_node, 4 bad_anchor, 5 anchor_not_on_origin, "
             "6 anchor_far_from_camera, 7 singular, 8 result_out_of_range, 9 hud_too_far\n");
    }

    DWORD WINAPI InitThread(LPVOID)
    {
        constexpr int kAttempts = 60;
        bool resolved = false;

        for (int attempt = 0; attempt < kAttempts && !resolved; ++attempt)
        {
            bool verbose = attempt == kAttempts - 1;
            resolved = nms::ResolveCore(g_addr, verbose) && nms::ResolveDiagnostics(g_addr, verbose);
            if (!resolved)
                Sleep(500);
        }

        if (!resolved)
        {
            Logf("INFO,0,0,0,signatures not found; probe inactive\n");
            LogFlush();
            return 0;
        }

        WriteHeader();

        if (!hooks::Init())
        {
            LogFlush();
            return 0;
        }

        hooks::Create(reinterpret_cast<void*>(g_addr.applyMatrix), reinterpret_cast<void*>(&HookApplyMatrix),
            reinterpret_cast<void**>(&g_originalApplyMatrix), "ApplyMatrix");
        hooks::Create(reinterpret_cast<void*>(g_addr.hudBuilder), reinterpret_cast<void*>(&HookHudBuilder),
            reinterpret_cast<void**>(&g_originalHudBuilder), "HudBuilder");
        hooks::Create(reinterpret_cast<void*>(g_addr.vrUpdate), reinterpret_cast<void*>(&HookVrUpdate),
            reinterpret_cast<void**>(&g_originalVrUpdate), "VrUpdate");
        openvr::Install(&OnPose);

        Logf("INFO,0,0,0,ready\n");
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
        InitializeCriticalSection(&g_stateLock);

        if (!LogOpen(module, L"FrameProbe", L"csv", true))
            return TRUE;

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
