#include "common/openvr_hook.h"

#include "common/hooks.h"
#include "common/log.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cstring>

namespace bvh::openvr
{
    namespace
    {
        // Minimal copies of the openvr.h types we touch.
        struct HmdMatrix34
        {
            float m[3][4];
        };

        struct HmdVector3
        {
            float v[3];
        };

        struct TrackedDevicePose
        {
            HmdMatrix34 deviceToAbsoluteTracking;
            HmdVector3 velocity;
            HmdVector3 angularVelocity;
            int trackingResult;
            bool poseIsValid;
            bool deviceIsConnected;
        };
        static_assert(sizeof(TrackedDevicePose) == 80, "TrackedDevicePose_t layout");

        using GetGenericInterfaceFn = void* (*)(const char* name, int* error);
        using WaitGetPosesFn = int (*)(
            void* self,
            TrackedDevicePose* renderPoses,
            uint32_t renderPoseCount,
            TrackedDevicePose* gamePoses,
            uint32_t gamePoseCount
        );

        // IVRCompositor vtable: SetTrackingSpace, GetTrackingSpace, WaitGetPoses, ...
        constexpr int kWaitGetPosesSlot = 2;
        // Version string NMS passes (read from its .rdata).
        constexpr const char* kCompositorVersion = "IVRCompositor_029";

        enum HookState : LONG { NotHooked = 0, Hooking = 1, HookedOk = 2, HookFailed = 3 };

        GetGenericInterfaceFn g_originalGetGenericInterface = nullptr;
        WaitGetPosesFn g_originalWaitGetPoses = nullptr;
        PoseCallback g_callback = nullptr;
        volatile LONG g_hookState = NotHooked;

        SRWLOCK g_poseLock = SRWLOCK_INIT;
        PoseSample g_latest;
        uint64_t g_sequence = 0;

        int HookWaitGetPoses(
            void* self,
            TrackedDevicePose* renderPoses,
            uint32_t renderPoseCount,
            TrackedDevicePose* gamePoses,
            uint32_t gamePoseCount
        )
        {
            int result = g_originalWaitGetPoses(self, renderPoses, renderPoseCount, gamePoses, gamePoseCount);

            PoseSample sample;
            sample.timeMicros = NowMicros();
            sample.threadId = GetCurrentThreadId();
            sample.result = result;

            if (renderPoses && renderPoseCount > 0)
            {
                const TrackedDevicePose& hmd = renderPoses[0];
                sample.valid = hmd.poseIsValid;
                std::memcpy(sample.m, hmd.deviceToAbsoluteTracking.m, sizeof(sample.m));
            }

            AcquireSRWLockExclusive(&g_poseLock);
            sample.sequence = ++g_sequence;
            g_latest = sample;
            ReleaseSRWLockExclusive(&g_poseLock);

            if (g_callback)
                g_callback(sample);

            return result;
        }

        void HookCompositor(void* compositor)
        {
            if (!compositor || InterlockedCompareExchange(&g_hookState, Hooking, NotHooked) != NotHooked)
                return;

            void** vtable = *reinterpret_cast<void***>(compositor);
            Logf("INFO,0,0,0,IVRCompositor=%p vtable=%p\n", compositor, static_cast<void*>(vtable));

            bool ok = hooks::Create(
                vtable[kWaitGetPosesSlot],
                reinterpret_cast<void*>(&HookWaitGetPoses),
                reinterpret_cast<void**>(&g_originalWaitGetPoses),
                "IVRCompositor::WaitGetPoses"
            );

            InterlockedExchange(&g_hookState, ok ? HookedOk : HookFailed);
        }

        void* HookGetGenericInterface(const char* name, int* error)
        {
            void* result = g_originalGetGenericInterface(name, error);

            if (result && name && std::strncmp(name, "IVRCompositor_", 14) == 0)
                HookCompositor(result);

            return result;
        }
    }

    bool Install(PoseCallback callback)
    {
        g_callback = callback;

        HMODULE openvr = GetModuleHandleW(L"openvr_api.dll");
        if (!openvr)
        {
            Logf("INFO,0,0,0,openvr_api.dll not loaded\n");
            return false;
        }

        void* target = reinterpret_cast<void*>(GetProcAddress(openvr, "VR_GetGenericInterface"));
        if (!target)
        {
            Logf("INFO,0,0,0,VR_GetGenericInterface export not found\n");
            return false;
        }

        return hooks::Create(
            target,
            reinterpret_cast<void*>(&HookGetGenericInterface),
            reinterpret_cast<void**>(&g_originalGetGenericInterface),
            "VR_GetGenericInterface"
        );
    }

    void TryHookExistingCompositor()
    {
        if (g_hookState != NotHooked || !g_originalGetGenericInterface)
            return;

        int error = 0;
        void* compositor = g_originalGetGenericInterface(kCompositorVersion, &error);
        if (compositor && error == 0)
            HookCompositor(compositor);
    }

    bool Hooked()
    {
        return g_hookState == HookedOk;
    }

    bool Latest(PoseSample& out)
    {
        AcquireSRWLockShared(&g_poseLock);
        out = g_latest;
        ReleaseSRWLockShared(&g_poseLock);
        return out.sequence != 0;
    }
}
