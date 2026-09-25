#include "common/nms.h"

#include "common/log.h"
#include "common/pattern.h"

#include <cstring>

namespace bvh::nms
{
    namespace
    {
        // Signatures verified unique against NMS.exe with PE timestamp 0x6AB0FFC9 (2026-09-21).

        // ApplyMatrix(handle, matrix): validate handle, look up node, tail-jump to the node's setter.
        // The trailing "mov rdx,r10; jmp" separates it from a sibling function with the same prologue.
        constexpr const char* kApplyMatrixSig =
            "44 8B C9 4C 8B D2 41 C1 E9 13 45 85 C9 74 ?? 44 8B C1 41 81 E0 FF FF 07 00 "
            "41 81 F8 FF FF 07 00 74 ?? 48 8B 15 ?? ?? ?? ?? 81 E1 FF FF 07 00 48 8B 82 E8 00 00 00 "
            "48 63 0C 88 48 8B 82 90 00 00 00 48 8B 0C C8 48 85 C9 74 ?? 8B 51 08 8B C2 25 FF FF 07 00 "
            "41 3B C0 75 ?? C1 EA 13 41 3B D1 75 ?? 49 8B D2 E9";
        constexpr int kApplyMatrixSceneLoad = 0x22;   // mov rdx,[rip+SceneManager]

        // Inside the HUD builder: "je; test eax,eax; je; lea rdx,[rsp+20]; call ApplyMatrix; movaps xmm13,[rsp+60]"
        constexpr const char* kHudCallSiteSig =
            "74 ?? 85 C0 74 ?? 48 8D 54 24 20 E8 ?? ?? ?? ?? 44 0F 28 6C 24 60 4C 8D 9C 24 E8 00 00 00";
        constexpr int kHudCallSiteCall = 0x0B;
        constexpr int kHudCallSiteReturn = 0x10;

        constexpr const char* kHudBuilderSig =
            "48 8B C4 48 81 EC E8 00 00 00 8B 51 14 8B 49 10 0F 29 70 E8 0F 29 78 D8 44 0F 29 40 C8";

        struct RipOperand
        {
            int offset;
            const char* opcode;
            int dispOffset;
            int length;
        };
        constexpr RipOperand kViewObjectLoad = { 0x51, "48 8B 05", 3, 7 };      // mov rax,[rip+view]
        constexpr RipOperand kHudVecBMul = { 0x107, "44 0F 59 25", 4, 8 };     // mulps xmm12,[rip+B]
        constexpr RipOperand kHudVecCAdd = { 0x1C9, "44 0F 58 25", 4, 8 };     // addps xmm12,[rip+C]
        constexpr RipOperand kHudVecAMul = { 0x24D, "0F 59 05", 3, 7 };        // mulps xmm0,[rip+A]

        // Main HUD update: "movaps xmm2,xmm6; movaps xmm1,xmm0; movaps [rsp+..],xmm13; mov rcx,r14; call wrapper; mov rbx,[rbp-..]"
        // The wrapper reads the HUD distance/scale settings and tail-jumps to the HUD builder.
        constexpr const char* kMainHudCallSig =
            "0F 28 D6 0F 28 C8 44 0F 29 AC 24 ?? ?? 00 00 49 8B CE E8 ?? ?? ?? ?? 48 8B 5D";
        constexpr int kMainHudCall = 0x12;
        constexpr int kMainHudCallReturn = 0x17;
        constexpr int kWrapperScanBytes = 0x100;

        constexpr const char* kVrUpdateSig =
            "40 55 53 56 48 8D AC 24 50 FF FF FF 48 81 EC B0 01 00 00";

        // Camera capture point used by the original CameraTransformLogger:
        // "movups xmm0,[r15+5B0]; add rcx,71C690; movaps [rip+copy],xmm0"
        constexpr const char* kCameraCaptureSig = "41 0F 10 87 B0 05 00 00 48 81 C1 90 C6 71 00 0F 29 05";
        constexpr RipOperand kCameraCopyStore = { 0x0F, "0F 29 05", 3, 7 };

        // No code reference found yet; only valid for the build above.
        constexpr uintptr_t kCameraMatrixRva = 0x6E7CA30;
        constexpr uint32_t kCameraMatrixBuild = 0x6AB0FFC9;

        // SceneManager layout (from the node setter at ApplyMatrix's jump target and its callees).
        constexpr size_t kLocalMatrices = 0x48;   // float[16] per slot, current frame
        constexpr size_t kWorldMatrices = 0x60;   // float[16] per slot, cached world transform
        constexpr size_t kHierarchy = 0x70;       // 20-byte records: +4 parent handle, +8 first child, +0x10 next sibling
        constexpr size_t kNodeObjects = 0x90;     // Node* per slot, node->handle at +8
        constexpr size_t kNodeTypes = 0xA0;       // uint8 per slot
        constexpr size_t kSlotTable = 0xE8;       // int32 per handle index
        constexpr size_t kFrameCounter = 0x240;   // int32
        constexpr size_t kHierarchyStride = 20;

        template <typename T>
        T ReadAt(const uint8_t* base, size_t offset)
        {
            T value;
            std::memcpy(&value, base + offset, sizeof(T));
            return value;
        }

        bool ResolveRip(uintptr_t function, const RipOperand& operand, uintptr_t& out, const char* name, bool verbose)
        {
            uintptr_t instruction = function + operand.offset;
            if (!BytesMatch(instruction, operand.opcode))
            {
                if (verbose)
                    Logf("INFO,0,0,0,operand %s: unexpected bytes at %p\n", name, reinterpret_cast<void*>(instruction));
                return false;
            }
            out = ResolveRipTarget(instruction, operand.dispOffset, operand.length);
            return true;
        }

        uintptr_t Find(const char* signature, const char* name, bool verbose)
        {
            int count = 0;
            uintptr_t address = FindPattern(signature, &count);
            if (!address && verbose)
                Logf("INFO,0,0,0,signature %s: %d matches (need exactly 1)\n", name, count);
            return address;
        }

        // True when the function at 'wrapper' tail-jumps (jmp rel32) to 'target' within its first bytes.
        bool TailJumpsTo(uintptr_t wrapper, uintptr_t target)
        {
            auto bytes = reinterpret_cast<const uint8_t*>(wrapper);
            for (int i = 0; i + 5 <= kWrapperScanBytes; ++i)
            {
                if (bytes[i] == 0xE9 && ResolveRipTarget(wrapper + i, 1, 5) == target)
                    return true;
            }
            return false;
        }
    }

    bool Resolve(Addresses& out, bool verbose)
    {
        Addresses a;
        a.base = MainModuleBase();
        a.timestamp = MainModuleTimestamp();

        a.applyMatrix = Find(kApplyMatrixSig, "ApplyMatrix", verbose);
        a.hudBuilder = Find(kHudBuilderSig, "HudBuilder", verbose);
        a.vrUpdate = Find(kVrUpdateSig, "VrUpdate", verbose);
        uintptr_t callSite = Find(kHudCallSiteSig, "HudCallSite", verbose);
        uintptr_t capture = Find(kCameraCaptureSig, "CameraCapture", verbose);
        uintptr_t mainHudCall = Find(kMainHudCallSig, "MainHudCall", verbose);

        if (!a.applyMatrix || !a.hudBuilder || !a.vrUpdate || !callSite || !capture || !mainHudCall)
            return false;

        uintptr_t mainHudWrapper = ResolveRipTarget(mainHudCall + kMainHudCall, 1, 5);
        if (!TailJumpsTo(mainHudWrapper, a.hudBuilder))
        {
            if (verbose)
                Logf("INFO,0,0,0,main HUD wrapper does not jump to the HUD builder\n");
            return false;
        }
        a.mainHudCallerReturn = mainHudCall + kMainHudCallReturn;

        if (ResolveRipTarget(callSite + kHudCallSiteCall, 1, 5) != a.applyMatrix)
        {
            if (verbose)
                Logf("INFO,0,0,0,HUD call site does not call ApplyMatrix\n");
            return false;
        }
        a.hudCallReturn = callSite + kHudCallSiteReturn;

        if (!BytesMatch(a.applyMatrix + kApplyMatrixSceneLoad, "48 8B 15"))
            return false;
        a.sceneManagerGlobal = ResolveRipTarget(a.applyMatrix + kApplyMatrixSceneLoad, 3, 7);

        if (!ResolveRip(a.hudBuilder, kViewObjectLoad, a.viewObjectGlobal, "ViewObject", verbose) ||
            !ResolveRip(a.hudBuilder, kHudVecAMul, a.hudVecA, "HudVecA", verbose) ||
            !ResolveRip(a.hudBuilder, kHudVecBMul, a.hudVecB, "HudVecB", verbose) ||
            !ResolveRip(a.hudBuilder, kHudVecCAdd, a.hudVecC, "HudVecC", verbose) ||
            !ResolveRip(capture, kCameraCopyStore, a.cameraCopy, "CameraCopy", verbose))
        {
            return false;
        }

        if (a.timestamp == kCameraMatrixBuild)
            a.cameraMatrix = a.base + kCameraMatrixRva;
        else if (verbose)
            Logf("INFO,0,0,0,unknown build 0x%08X: camera matrix address unknown\n", a.timestamp);

        out = a;
        return true;
    }

    bool IsLiveHandle(uint32_t handle)
    {
        uint32_t index = handle & 0x7FFFF;
        return (handle >> 19) != 0 && index != 0x7FFFF;
    }

    bool Scene::Refresh()
    {
        manager_ = *reinterpret_cast<const uint8_t* const*>(global_);
        return manager_ != nullptr;
    }

    int Scene::SlotOf(uint32_t handle) const
    {
        if (!manager_ || !IsLiveHandle(handle))
            return -1;

        auto slots = ReadAt<const int32_t*>(manager_, kSlotTable);
        auto nodes = ReadAt<const uint8_t* const*>(manager_, kNodeObjects);
        if (!slots || !nodes)
            return -1;

        int32_t slot = slots[handle & 0x7FFFF];
        if (slot < 0)
            return -1;

        const uint8_t* node = nodes[slot];
        if (!node || ReadAt<uint32_t>(node, 8) != handle)
            return -1;

        return slot;
    }

    const float* Scene::Local(int slot) const
    {
        return ReadAt<const float*>(manager_, kLocalMatrices) + slot * 16;
    }

    const float* Scene::World(int slot) const
    {
        return ReadAt<const float*>(manager_, kWorldMatrices) + slot * 16;
    }

    uint32_t Scene::ParentHandle(int slot) const
    {
        auto records = ReadAt<const uint8_t*>(manager_, kHierarchy);
        return ReadAt<uint32_t>(records, slot * kHierarchyStride + 4);
    }

    uint8_t Scene::Type(int slot) const
    {
        return ReadAt<const uint8_t*>(manager_, kNodeTypes)[slot];
    }

    const void* Scene::NodeObject(int slot) const
    {
        return ReadAt<const void* const*>(manager_, kNodeObjects)[slot];
    }

    int32_t Scene::Frame() const
    {
        return manager_ ? ReadAt<int32_t>(manager_, kFrameCounter) : -1;
    }
}
