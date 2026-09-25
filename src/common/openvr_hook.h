#pragma once

#include <cstdint>

// Captures the HMD pose the game receives from IVRCompositor::WaitGetPoses.
namespace bvh::openvr
{
    struct PoseSample
    {
        uint64_t sequence = 0;   // increments per WaitGetPoses call
        int64_t timeMicros = 0;
        uint32_t threadId = 0;
        int result = 0;          // EVRCompositorError
        bool valid = false;
        float m[3][4] = {};      // HMD mDeviceToAbsoluteTracking (column-vector convention)
    };

    using PoseCallback = void (*)(const PoseSample& sample);

    // Hooks openvr_api.dll!VR_GetGenericInterface to catch the compositor; call once hooks::Init() succeeded.
    bool Install(PoseCallback callback);

    // Fallback when the game fetched the compositor before Install(): asks openvr_api directly.
    void TryHookExistingCompositor();

    bool Hooked();
    bool Latest(PoseSample& out);
}
