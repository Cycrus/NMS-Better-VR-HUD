#include "common/hud_lock.h"

#include "common/mat4.h"

namespace bvh
{
    namespace
    {
        // The anchor sits at the head; anything further away means it is in a different space.
        constexpr float kMaxAnchorDistance = 50.0f;
        constexpr float kMaxLocalTranslation = 50.0f;
        constexpr float kIdentityTolerance = 1e-3f;
    }

    const char* LockStatusName(LockStatus status)
    {
        switch (status)
        {
            case LockStatus::Ok: return "ok";
            case LockStatus::NoCamera: return "no_camera";
            case LockStatus::NoScene: return "no_scene";
            case LockStatus::BadNode: return "bad_node";
            case LockStatus::BadAnchor: return "bad_anchor";
            case LockStatus::AnchorNotOnOrigin: return "anchor_not_on_origin";
            case LockStatus::AnchorFarFromCamera: return "anchor_far_from_camera";
            case LockStatus::Singular: return "singular";
            case LockStatus::ResultOutOfRange: return "result_out_of_range";
        }
        return "unknown";
    }

    LockStatus ComputeHeadLockedLocal(const nms::Addresses& addresses, uint32_t handle, const float* offset, float* out)
    {
        if (!addresses.cameraMatrix)
            return LockStatus::NoCamera;
        const float* camera = reinterpret_cast<const float*>(addresses.cameraMatrix);

        nms::Scene scene(addresses.sceneManagerGlobal);
        if (!scene.Refresh())
            return LockStatus::NoScene;

        int slot = scene.SlotOf(handle);
        if (slot < 0)
            return LockStatus::BadNode;

        int anchorSlot = scene.SlotOf(scene.ParentHandle(slot));
        if (anchorSlot < 0)
            return LockStatus::BadAnchor;

        int originSlot = scene.SlotOf(scene.ParentHandle(anchorSlot));
        if (originSlot < 0 || !mat4::IsPureTranslation(scene.World(originSlot), kIdentityTolerance))
            return LockStatus::AnchorNotOnOrigin;

        const float* anchor = scene.Local(anchorSlot);
        if (mat4::TranslationDistance(anchor, camera) > kMaxAnchorDistance)
            return LockStatus::AnchorFarFromCamera;

        float inverseAnchor[16];
        if (!mat4::InverseAffine(anchor, inverseAnchor))
            return LockStatus::Singular;

        float desired[16];
        mat4::MultiplyAffine(offset, camera, desired);
        mat4::MultiplyAffine(desired, inverseAnchor, out);

        out[3] = offset[3];
        out[7] = offset[7];
        out[11] = offset[11];
        out[15] = offset[15];

        if (!mat4::IsFinite(out) || mat4::TranslationLength(out) > kMaxLocalTranslation)
            return LockStatus::ResultOutOfRange;

        return LockStatus::Ok;
    }
}
