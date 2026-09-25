#pragma once

namespace bvh::hooks
{
    bool Init();
    // Creates and enables a MinHook detour; logs the outcome.
    bool Create(void* target, void* detour, void** original, const char* name);
}
