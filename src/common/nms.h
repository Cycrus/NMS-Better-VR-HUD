#pragma once

#include <cstdint>

// Everything that depends on the No Man's Sky binary lives here: signatures, RIP-relative
// globals and the layout of the engine's scene-graph manager.
namespace bvh::nms
{
    // Sets a scene node's local matrix (row-major float[16]) by handle.
    using ApplyMatrixFn = void (*)(uint32_t handle, const float* matrix);
    // Builds a node's local matrix from three scalars and applies it. One of its call sites places the HUD.
    using HudBuilderFn = void (*)(void* object, float a, float b, float c);
    // Per-frame VR event pump (polls IVRSystem events), runs on the game thread.
    using VrUpdateFn = void (*)(void* self);

    struct Addresses
    {
        uintptr_t base = 0;
        uint32_t timestamp = 0;

        uintptr_t applyMatrix = 0;
        uintptr_t hudCallReturn = 0;     // return address of the HUD builder's ApplyMatrix call
        uintptr_t hudBuilder = 0;
        uintptr_t mainHudCallerReturn = 0; // HUD builder's caller for the main HUD panel (via a tail-jumping wrapper)
        uintptr_t vrUpdate = 0;

        uintptr_t sceneManagerGlobal = 0; // SceneManager*
        uintptr_t viewObjectGlobal = 0;   // active view object*, compared against "APPVIEW" by the HUD builder
        uintptr_t hudVecA = 0;            // float4 globals the HUD builder mixes into the translation
        uintptr_t hudVecB = 0;
        uintptr_t hudVecC = 0;
        uintptr_t cameraCopy = 0;         // 80 bytes copied every frame at the camera capture point
        uintptr_t cameraMatrix = 0;       // float[16]: camera world matrix (found with Cheat Engine, no code xref yet)
    };

    // Resolves all addresses. Returns false (and logs why, if verbose) when any signature fails.
    bool Resolve(Addresses& out, bool verbose);

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
