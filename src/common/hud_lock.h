#pragma once

#include <cstdint>

#include "common/nms.h"

namespace bvh
{
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
    };

    const char* LockStatusName(LockStatus status);

    // Head-locks a HUD node by keeping the game's local matrix as an offset from the camera
    // instead of from the node's anchor.
    //
    // HUD nodes hang off an anchor (head position, tracking-space orientation) whose parent is the
    // floating-origin node, so the anchor's local matrix is in the same origin-relative space as the
    // camera matrix:
    //     local = offset * camera * inverse(anchorLocal)
    // The engine's column-3 values of the offset are kept in the result.
    LockStatus ComputeHeadLockedLocal(const nms::Addresses& addresses, uint32_t handle, const float* offset, float* out);
}
