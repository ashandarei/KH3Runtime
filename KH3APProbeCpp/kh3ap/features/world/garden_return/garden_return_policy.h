#pragma once

#include "kh3ap/features/kh02/core/kh02_host_world.h"
#include "kh3ap/features/world/space/space_policy.h"
#include <string_view>

namespace kh3ap
{
    constexpr bool camp_return_requires_corona_landing(std::wstring_view world)
    {
        return world.starts_with(L"World /Game/Levels/ra/");
    }

    constexpr bool camp_return_corona_landing_allowed(std::wstring_view world, bool first_save_point_available)
    {
        return !camp_return_requires_corona_landing(world) || first_save_point_available;
    }

    constexpr bool camp_is_kh02_world(std::wstring_view world)
    {
        return is_kh02_host_world(world);
    }
    constexpr bool camp_return_world_allowed(std::wstring_view world)
    {
        // Prepare & Retry opens camp in the opening Darkside arena. Leaving
        // here would bypass the encounter and its normal transition to Garden.
        if (world == L"World /Game/Levels/ew_DLC/ew_04/ew_04.ew_04") return false;
        return camp_is_kh02_world(world) || space::camp_returns_to_space(world) || world.find(L"/Game/Levels/") != world.npos;
    }
}
