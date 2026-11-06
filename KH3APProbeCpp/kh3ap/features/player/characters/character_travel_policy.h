#pragma once

namespace kh3ap::character_swap
{
    constexpr int travel_target_after_swap(int retained, int incoming, int sora, bool cinematic)
    {
        return cinematic ? retained : incoming == sora ? -1 : incoming;
    }
}
