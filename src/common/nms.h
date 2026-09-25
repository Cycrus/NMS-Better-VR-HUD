#pragma once

#include <cstdint>

// Everything that depends on the No Man's Sky binary lives here: signatures, RIP-relative
// globals and the layout of the engine's scene-graph manager.
namespace bvh::nms
{
    // Sets a scene node's local matrix (row-major float[16]) by handle.
    using ApplyMatrixFn = void (*)(uint32_t handle, const float* matrix);
    // Builds a node's local matrix from (distance, 0, scale) and applies it. One of its call sites places the HUD.
    using HudBuilderFn = void (*)(void* object, float a, float b, float c);
    // Per-frame VR event pump (polls IVRSystem events), runs on the game thread.
    using VrUpdateFn = void (*)(void* self);

    struct Addresses
    {
        uintptr_t base = 0;
        uint32_t timestamp = 0;

        // Core: needed to head-lock the HUD.
        uintptr_t applyMatrix = 0;
        uintptr_t hudCallReturn = 0;       // return address of the HUD builder's ApplyMatrix call
        uintptr_t hudBuilder = 0;
        uintptr_t mainHudCallerReturn = 0; // HUD builder's caller for the main HUD panel (via a tail-jumping wrapper)
        uintptr_t sceneManagerGlobal = 0;  // SceneManager*
        uintptr_t viewObjectGlobal = 0;    // active view object*; its camera matrix is at +0x510

        // Diagnostics: only FrameProbe uses these.
        uintptr_t vrUpdate = 0;
        uintptr_t hudVecA = 0;             // float4 globals the HUD builder mixes into the translation
        uintptr_t hudVecB = 0;
        uintptr_t hudVecC = 0;
        uintptr_t cameraCopy = 0;          // 80 bytes copied every frame at the camera capture point
    };

    // Resolve the core addresses / the diagnostics addresses. Return false (and log why, if verbose)
    // when any signature fails; 'out' is only written on success.
    bool ResolveCore(Addresses& out, bool verbose);
    bool ResolveDiagnostics(Addresses& out, bool verbose);

    // Camera world matrix of the active view (float[16], origin-relative), or null before one exists.
    const float* CameraMatrix(const Addresses& addresses);

    bool IsLiveHandle(uint32_t handle);

    // Read-only view of the scene-graph manager. Arrays are indexed by slot (handle index -> slot table).
    class Scene
    {
    public:
        explicit Scene(uintptr_t managerGlobal) : global_(managerGlobal) {}

        bool Refresh();
        int SlotOf(uint32_t handle) const;

        const float* Local(int slot) const;
        const float* World(int slot) const;
        uint32_t ParentHandle(int slot) const;
        uint8_t Type(int slot) const;
        const void* NodeObject(int slot) const;
        int32_t Frame() const;

    private:
        uintptr_t global_;
        const uint8_t* manager_ = nullptr;
    };
}
