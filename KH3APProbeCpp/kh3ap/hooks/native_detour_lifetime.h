#pragma once
#include <polyhook2/Detour/x64Detour.hpp>
#include <memory>

namespace kh3ap
{
    // A live detour still owns executable trampoline storage. Keep that storage
    // and its callback context alive if restoring the original code fails.
    inline bool release_native_detour(std::unique_ptr<PLH::x64Detour>& hook)
    {
        if (hook && hook->isHooked() && !hook->unHook())
        {
            return false;
        }
        hook.reset();
        return true;
    }
}
