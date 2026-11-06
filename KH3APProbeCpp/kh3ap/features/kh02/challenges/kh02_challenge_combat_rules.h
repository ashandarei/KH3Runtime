#pragma once
#include "kh02_challenge_progress_rules.h"
#include <string_view>

namespace kh3ap::kh02_tracking
{
    inline constexpr int first_magic_element = 1;
    inline constexpr int last_magic_element = 3;

    inline int species(std::wstring_view name)
    {
        constexpr std::wstring_view prefixes[]{L"e_ex001_Pawn", L"e_ex081_Pawn", L"e_ex003_Pawn",
                                               L"e_ex016_Pawn", L"e_ex004_Pawn", L"e_ex005_Pawn"};
        for (int i = 0; i < static_cast<int>(std::size(prefixes)); ++i)
        {
            if (name.starts_with(prefixes[i]) && (name.size() == prefixes[i].size() || name[prefixes[i].size()] == '_'))
            {
                return i;
            }
        }
        return -1;
    }

    inline bool heartless(std::wstring_view name)
    {
        if (species(name) >= 0)
        {
            return true;
        }
        for (std::wstring_view prefix : {L"e_dw401_Pawn", L"e_dw402_Pawn", L"e_dw407_Pawn", L"e_dw407b_Pawn"})
        {
            if (name.starts_with(prefix) && (name.size() == prefix.size() || name[prefix.size()] == '_'))
            {
                return true;
            }
        }
        return false;
    }

    struct Hit
    {
        bool player{}, magic{}, airborne{};
        int element{-1};
        bool lethal{};
        bool counter{}, spellweaver{}, style{};
    };

    inline bool defeat(std::map<int, int>& states, std::map<int, int>& counts, std::wstring_view enemy, const Hit& hit)
    {
        const int type = species(enemy);
        bool changed = false;
        if (hit.player && hit.lethal)
        {
            if (enemy.starts_with(L"e_dw402_Pawn_") && hit.spellweaver)
            {
                changed |= complete(states, kh02_challenge_id::finish_magic_wish_devil_tower);
            }
            if (enemy.starts_with(L"e_dw407b_Pawn_"))
            {
                changed |= add(states, counts, kh02_challenge_id::all_kill_shooter_darkside);
            }
        }
        if (!heartless(enemy))
        {
            return changed;
        }
        changed |= type >= 0 ? add(states, counts, type) : false;
        if (!hit.player || !hit.lethal)
        {
            return changed;
        }
        if (hit.magic && hit.element >= first_magic_element && hit.element <= last_magic_element)
        {
            changed |= add(states, counts, kh02_challenge_id::firaga_finish + hit.element - first_magic_element);
        }
        if (hit.airborne)
        {
            changed |= add(states, counts, kh02_challenge_id::flight_battle);
        }
        if (hit.counter)
        {
            changed |= add(states, counts, kh02_challenge_id::baria_finish);
        }
        if (hit.style)
        {
            changed |= add(states, counts, kh02_challenge_id::style_change_finish);
        }
        return changed;
    }

} // namespace kh3ap::kh02_tracking
