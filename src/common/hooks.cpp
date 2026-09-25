#include "common/hooks.h"

#include "common/log.h"

#include "MinHook.h"

namespace bvh::hooks
{
    bool Init()
    {
        MH_STATUS status = MH_Initialize();
        if (status != MH_OK && status != MH_ERROR_ALREADY_INITIALIZED)
        {
            Logf("MH_Initialize failed: %s\n", MH_StatusToString(status));
            return false;
        }
        return true;
    }

    bool Create(void* target, void* detour, void** original, const char* name)
    {
        MH_STATUS status = MH_CreateHook(target, detour, original);
        if (status == MH_OK)
            status = MH_EnableHook(target);

        Logf("hook %s at %p: %s\n", name, target, MH_StatusToString(status));
        return status == MH_OK;
    }
}
