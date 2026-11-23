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

    inline bool enemy_family(std::wstring_view name, std::wstring_view prefix)
    {
        return name.starts_with(prefix) && (name.size() == prefix.size() || name[prefix.size()] == L'_');
    }

    inline bool darkside(std::wstring_view name)
    {
        return enemy_family(name, L"e_dw407_Pawn") || enemy_family(name, L"e_dw407a_Pawn") ||
               enemy_family(name, L"e_dw407b_Pawn");
    }

    inline bool heartless(std::wstring_view name)
    {
        if (species(name) >= 0)
        {
            return true;
        }
        return enemy_family(name, L"e_dw401_Pawn") || enemy_family(name, L"e_dw402_Pawn") ||
               enemy_family(name, L"e_dw407_Pawn") || enemy_family(name, L"e_dw407b_Pawn");
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
        // The defeat journal has already confirmed and deduplicated the death.
        // Enemy-family totals do not depend on the damage event's owner.
        if (darkside(enemy))
        {
            changed |= add(states, counts, kh02_challenge_id::all_kill_shooter_darkside);
        }
        if (enemy_family(enemy, L"e_dw402_Pawn") && hit.player && hit.lethal && hit.spellweaver)
        {
            changed |= complete(states, kh02_challenge_id::finish_magic_wish_devil_tower);
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
