#include "common/hud_lock.h"

#include "common/mat4.h"

namespace bvh
{
    namespace
    {
        // The anchor sits at or just in front of the head; anything further means another space.
        constexpr float kMaxAnchorDistance = 50.0f;
        // The HUD sits 0.75 m (cockpit) to 2.5 m (on foot) from the eyes at default settings.
        constexpr float kMaxHudDistance = 10.0f;
        constexpr float kMaxLocalTranslation = 100.0f;
        constexpr float kIdentityTolerance = 1e-3f;
        constexpr float kCameraAxisTolerance = 0.05f;
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
            case LockStatus::HudTooFar: return "hud_too_far";
        }
        return "unknown";
    }

    LockStatus ComputeHeadLockedLocal(const nms::Addresses& addresses, uint32_t handle, const float* offset, float* out)
    {
        const float* camera = nms::CameraMatrix(addresses);
        if (!camera || !mat4::HasUnitAxes(camera, kCameraAxisTolerance))
            return LockStatus::NoCamera;

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

        float neutralHead[16];
        float inverseNeutral[16];
        float inverseAnchor[16];
        if (!mat4::OrientationAt(anchor, camera, neutralHead) ||
            !mat4::InverseAffine(neutralHead, inverseNeutral) ||
            !mat4::InverseAffine(anchor, inverseAnchor))
        {
            return LockStatus::Singular;
        }

        float hud[16];
        float hudInHead[16];
        mat4::MultiplyAffine(offset, anchor, hud);
        mat4::MultiplyAffine(hud, inverseNeutral, hudInHead);
        if (mat4::TranslationLength(hudInHead) > kMaxHudDistance)
            return LockStatus::HudTooFar;

        float desired[16];
        mat4::MultiplyAffine(hudInHead, camera, desired);
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
