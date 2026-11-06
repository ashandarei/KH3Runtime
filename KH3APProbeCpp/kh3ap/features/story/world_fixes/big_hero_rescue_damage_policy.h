#pragma once

#include <array>
#include <string_view>

namespace kh3ap
{
    // Match ancestors, so the five elemental and four pole-leg variants inherit
    // the rule. Never match the shared base: Wasabi and Go Go use that too.
    inline constexpr std::array<std::wstring_view, 4> big_hero_rescue_damage_classes{
        L"/Game/Blueprints/Gimmick/bx/g_bx_darkcube/g_bx_darkcube_03.g_bx_darkcube_03_C",
        L"/Game/Blueprints/Gimmick/bx/g_bx_darkcube/g_bx_darkcube_03_parts.g_bx_darkcube_03_parts_C",
        L"/Game/Blueprints/Gimmick/bx/g_bx_darkcube/g_bx_darkcube_04.g_bx_darkcube_04_C",
        L"/Game/Blueprints/Gimmick/bx/g_bx_darkcube/g_bx_darkcube_04_leg.g_bx_darkcube_04_leg_C",
    };

    constexpr bool block_big_hero_rescue_damage(std::wstring_view component, std::wstring_view ancestor)
    {
        // The Magic component keeps its original per-tendril attribute filter.
        // Its BP_ForceDestroy and the pole's scripted Dead calls bypass this
        // damage entry point and still drive the original rescue completion.
        if (component != L"TresGimmickHitCount")
        {
            return false;
        }
        for (const auto target : big_hero_rescue_damage_classes)
        {
            if (ancestor == target)
            {
                return true;
            }
        }
        return false;
    }
}
