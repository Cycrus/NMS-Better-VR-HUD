#include "nms_signatures.h"

#include <cstdint>

/**
 * A unique opcode signature close to the applyMatrix address
  * we want to hook in.
 */
static constexpr const char* APPLY_MATRIX_SIGNATURE =
    "48 8D 54 24 20 E8 ?? ?? ?? ?? 44 0F 28 6C 24 60 "
    "4C 8D 9C 24 E8 00 00 00";

/**
 * A unique opcode signature close to the body transform matrix
  * generation address we want to hook in.
 */
static constexpr const char* BODY_CAPTURE_POINT_SIGNATURE =
    "41 0F 11 87 B0 05 00 00 "
    "0F 10 48 10 "
    "41 0F 11 8F C0 05 00 00 "
    "0F 10 40 20 "
    "41 0F 11 87 D0 05 00 00 "
    "0F 10 48 30 "
    "41 0F 11 8F E0 05 00 00 "
    "0F 10 40 40 "
    "41 0F 11 87 F0 05 00 00 "
    "48 8B 0D ?? ?? ?? ?? "
    "48 81 C1 ?? ?? ?? ?? "
    "48 8B 01 "
    "FF 50 70";

/**
 * A unique opcode signature close to the camera transform matrix
 * generation address we want to hook in.
 */
static constexpr const char* CAMERA_CAPTURE_POINT_SIGNATURE =
    "41 0F 10 87 60 05 00 00 "
    "41 0F 10 8F 70 05 00 00 "
    "41 0F 11 87 10 05 00 00 "
    "41 0F 10 87 80 05 00 00 "
    "41 0F 11 8F 20 05 00 00 "
    "41 0F 10 8F 90 05 00 00 "
    "41 0F 11 87 30 05 00 00 "
    "41 0F 10 87 A0 05 00 00 "
    "41 0F 11 8F 40 05 00 00 "
    "41 0F 11 87 50 05 00 00 "
    "48 8B 05 ?? ?? ?? ?? "
    "C6 80 ?? ?? ?? ?? ?? "
    "48 8B 0D ?? ?? ?? ?? "
    "41 0F 10 87 B0 05 00 00 "
    "48 81 C1 ?? ?? ?? ?? "
    "0F 29 05 ?? ?? ?? ?? "
    "41 0F 10 8F C0 05 00 00 "
    "0F 29 0D ?? ?? ?? ?? "
    "41 0F 10 87 D0 05 00 00 "
    "0F 29 05 ?? ?? ?? ??";

/**
 * A unique opcode signature close to the vr update address we want
 * to hook in.
 */
static constexpr const char* VR_UPDATE_SIGNATURE =
    "80 3D ?? ?? ?? ?? 00 "
    "0F 85 ?? ?? ?? ?? "
    "80 3D ?? ?? ?? ?? 00 "
    "0F 85 ?? ?? ?? ?? "
    "80 B9 84 16 00 00 00 "
    "75 05 "
    "E8 ?? ?? ?? ?? "
    "48 8B 0D ?? ?? ?? ?? "
    "48 8B 01 "
    "FF 10 "
    "84 C0";

/**
 * The constant offset from the unique signature to reach the applyMatrix
 * hooking point.
 */
static constexpr size_t APPLY_MATRIX_CALL_OFFSET = 0x05;

/**
 * The constant offset from the unique signature to reach the body generation
 * transform hooking point.
 */
static constexpr size_t BODY_CAPTURE_POINT_HOOK_OFFSET = 0x38;

/**
 * The constant offset from the unique signature to reach the camera generation
 * transform hooking point.
 */
static constexpr size_t CAMERA_CAPTURE_POINT_HOOK_OFFSET = 0x65;

/**
 * The constant offset from the unique signature to reach the vr update hooking
 * point.
 */
static constexpr size_t VR_UPDATE_CALL_OFFSET = 0x23;


bool ResolveHookTargets(HookTargets* targets, SignatureScannerLogFn log)
{
    ModuleExecutableSections module = {};
    if (!GetMainModuleExecutableSections(&module, log))
        return false;

    uintptr_t applyMatrix = 0;
    uintptr_t bodyCapturePoint = 0;
    uintptr_t cameraCapturePoint = 0;
    uintptr_t vrUpdate = 0;

    if (!ResolveCallRel32Target(
            module,
            "APPLY_MATRIX",
            APPLY_MATRIX_SIGNATURE,
            APPLY_MATRIX_CALL_OFFSET,
            &applyMatrix,
            log
        ))
        return false;

    if (!ResolvePatternOffsetTarget(
            module,
            "BODY_CAPTURE_POINT",
            BODY_CAPTURE_POINT_SIGNATURE,
            BODY_CAPTURE_POINT_HOOK_OFFSET,
            &bodyCapturePoint,
            log
        ))
        return false;

    if (!ResolvePatternOffsetTarget(
            module,
            "CAMERA_CAPTURE_POINT",
            CAMERA_CAPTURE_POINT_SIGNATURE,
            CAMERA_CAPTURE_POINT_HOOK_OFFSET,
            &cameraCapturePoint,
            log
        ))
        return false;

    if (!ResolveCallRel32Target(
            module,
            "VR_UPDATE",
            VR_UPDATE_SIGNATURE,
            VR_UPDATE_CALL_OFFSET,
            &vrUpdate,
            log
        ))
        return false;

    targets->applyMatrix = reinterpret_cast<void*>(applyMatrix);
    targets->bodyCapturePoint = reinterpret_cast<void*>(bodyCapturePoint);
    targets->cameraCapturePoint = reinterpret_cast<void*>(cameraCapturePoint);
    targets->vrUpdate = reinterpret_cast<void*>(vrUpdate);
    return true;
}
