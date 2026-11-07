#pragma once
#include <string_view>

namespace kh3ap::character_swap
{
    constexpr bool cinematic_requires_native_sora(std::wstring_view world)
    {
        // Caribbean movies also initialize the moving ship/deck gameplay state.
        // Rebinding a Sora mesh role does not transfer that gameplay ownership.
        return world.find(L"/Game/Levels/ca/") != world.npos;
    }

    constexpr int travel_target_after_swap(int retained, int incoming, int sora, bool cinematic)
    {
        return cinematic ? retained : incoming == sora ? -1 : incoming;
    }
}
