#pragma once

#include <cstdint>

#include "common/nms.h"

namespace bvh
{
    // Numeric values appear in FrameProbe logs; append new ones at the end.
    enum class LockStatus : int
    {
        Ok = 0,
        NoCamera,
        NoScene,
        BadNode,
        BadAnchor,
        AnchorNotOnOrigin,     // anchor's parent is not the floating-origin node
        AnchorFarFromCamera,   // anchor and camera are not in the same space
        Singular,
        ResultOutOfRange,
        HudTooFar,             // game layout puts the HUD implausibly far from the eyes (e.g. mid-transition)
    };

    const char* LockStatusName(LockStatus status);

    // Head-locks a HUD node: keeps the game's layout as seen when looking straight ahead, and turns it
    // with the head.
    //
    // HUD nodes hang off an anchor whose parent is the floating-origin node, so the anchor's local
    // matrix is in the same origin-relative space as the camera. On foot the anchor sits at the eyes
    // with the tracking-space orientation; in a cockpit it sits in front of the eyes, scaled down.
    // "Straight ahead" is the anchor's orientation at the eye position (N), so
    //     local = offset * anchor * inverse(N) * camera * inverse(anchor)
    // The engine's column-3 values of the offset are kept in the result.
    LockStatus ComputeHeadLockedLocal(const nms::Addresses& addresses, uint32_t handle, const float* offset, float* out);
}
