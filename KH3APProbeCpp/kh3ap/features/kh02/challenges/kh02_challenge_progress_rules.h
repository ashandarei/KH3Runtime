#pragma once
#include "kh02_challenge_ids.h"
#include <algorithm>
#include <map>
#include <array>
#include <string_view>

namespace kh3ap::kh02_tracking
{
    inline constexpr int mickey_link_state_id = 113;
    inline constexpr int ice_slide_goal_meters = 75;
    inline constexpr int centimeters_per_meter = 100;
    inline constexpr int critical_difficulty = 3;
    inline constexpr int maximum_player_level = 99;
    inline constexpr int excellent_shotlock_chain_goal = 6;

    // One commit path for rule mutations. Callers keep their audited guards and
    // return semantics; writers introduce no reads or extra map insertions.
    namespace progress_mutation
    {
        inline void state(std::map<int, int>& states, int id, int value)
        {
            states[id] = value;
        }

        inline void count(std::map<int, int>& counts, int id, int value)
        {
            counts[id] = value;
        }

        inline void counted(std::map<int, int>& states, std::map<int, int>& counts, int id, int value, int status)
        {
            count(counts, id, value);
            state(states, id, status);
        }
    } // namespace progress_mutation

    // Every counted objective is associated with its named original table ID.
    // All elemental spell tiers qualify, matching the authored descriptions.
    inline constexpr auto objective_goals = []
    {
        std::array<int, kh02_challenge_id::count> targets{};
        targets[kh02_challenge_id::kill_shadow] = 30;
        targets[kh02_challenge_id::kill_kurokosagi] = 45;
        targets[kh02_challenge_id::kill_flame_jem] = 40;
        targets[kh02_challenge_id::kill_neo_shadow] = 30;
        targets[kh02_challenge_id::kill_water_jem] = 20;
        targets[kh02_challenge_id::kill_mud_jem] = 20;
        targets[kh02_challenge_id::level_up_low] = 60;
        targets[kh02_challenge_id::level_up_middle] = 70;
        targets[kh02_challenge_id::level_up_high] = 80;
        targets[kh02_challenge_id::baria_finish] = 5;
        targets[kh02_challenge_id::firaga_finish] = 30;
        targets[kh02_challenge_id::blizaga_finish] = 30;
        targets[kh02_challenge_id::thundaga_finish] = 50;
        targets[kh02_challenge_id::blizaga_rail_slide] = ice_slide_goal_meters;
        targets[kh02_challenge_id::kill_streak_blizaga] = 5;
        targets[kh02_challenge_id::weak_point_attack] = 1;
        targets[kh02_challenge_id::flight_battle] = 5;
        targets[kh02_challenge_id::excellent_shoot] = excellent_shotlock_chain_goal;
        targets[kh02_challenge_id::style_change_finish] = 50;
        targets[kh02_challenge_id::first_time_magic_wish] = 1;
        targets[kh02_challenge_id::all_kill_shooter_darkside] = 3;
        targets[kh02_challenge_id::coop_action] = 5;
        targets[kh02_challenge_id::get_all_star_sign] = 12;
        targets[kh02_challenge_id::get_all_treasure_dark_area] = 5;
        targets[kh02_challenge_id::attack_streat_light] = 20;
        targets[kh02_challenge_id::get_white_gem_mirror_area] = 7;
        targets[kh02_challenge_id::destroy_thorn] = 50;
        targets[kh02_challenge_id::check_all_flower] = 3;
        targets[kh02_challenge_id::get_all_treasure_gear_area] = 5;
        targets[kh02_challenge_id::get_all_treasure_mirror_area] = 8;
        targets[kh02_challenge_id::get_all_treasure_thorn_area] = 12;
        return targets;
    }();

    inline int goal(int id)
    {
        return id >= 0 && id < static_cast<int>(objective_goals.size()) ? objective_goals[id] : 0;
    }

    inline bool complete(std::map<int, int>& states, int id)
    {
        if (states[id] == kh02_challenge_state::complete)
        {
            return false;
        }
        progress_mutation::state(states, id, kh02_challenge_state::complete);
        return true;
    }

    inline bool mickey_link_state(int state_id)
    {
        return state_id == mickey_link_state_id;
    }

    inline bool spellweaver_activation(std::map<int, int>& states, std::map<int, int>& counts, bool full_wardrobe,
                                       bool phantom_battle)
    {
        bool changed = complete(states, kh02_challenge_id::first_time_magic_wish);
        if (changed)
        {
            progress_mutation::count(counts, kh02_challenge_id::first_time_magic_wish, 1);
        }
        if (full_wardrobe)
        {
            changed |= complete(states, kh02_challenge_id::magic_wish_with_all_accessories);
        }
        if (phantom_battle)
        {
            changed |= complete(states, kh02_challenge_id::finish_magic_wish_enemy_aqua);
        }
        return changed;
    }

    inline bool critical_tide_victory(std::map<int, int>& states, int difficulty)
    {
        return difficulty == critical_difficulty && complete(states, kh02_challenge_id::finish_coop_action_devil_wave);
    }

    inline bool add(std::map<int, int>& states, std::map<int, int>& counts, int id, int amount = 1)
    {
        const int target = goal(id);
        if (!target || amount < 1 || states[id] == kh02_challenge_state::complete)
        {
            return false;
        }
        const int next = std::min(target, counts[id] + std::min(amount, target));
        progress_mutation::counted(states, counts, id, next,
                                   next >= target ? kh02_challenge_state::complete : kh02_challenge_state::in_progress);
        return true;
    }

    inline bool level(std::map<int, int>& states, std::map<int, int>& counts, int current)
    {
        if (current < 1 || current > maximum_player_level)
        {
            return false;
        }
        bool changed = false;
        for (int id = kh02_challenge_id::level_up_low; id <= kh02_challenge_id::level_up_high; ++id)
        {
            const int n = std::min(current, goal(id));
            if (counts[id] < n)
            {
                progress_mutation::counted(states, counts, id, n,
                                           n >= goal(id) ? kh02_challenge_state::complete
                                                         : kh02_challenge_state::in_progress);
                changed = true;
            }
        }
        return changed;
    }

    // A miss breaks the Excellent chain. Multiple updates of the same native
    // Shotlock result are deduplicated by the caller's consumed-result event.
    inline bool shotlock_result(std::map<int, int>& states, std::map<int, int>& counts, bool excellent)
    {
        if (states[kh02_challenge_id::excellent_shoot] == kh02_challenge_state::complete)
        {
            return false;
        }
        const int next =
            excellent ? std::min(excellent_shotlock_chain_goal, counts[kh02_challenge_id::excellent_shoot] + 1) : 0;
        const bool changed = counts[kh02_challenge_id::excellent_shoot] != next;
        progress_mutation::counted(states, counts, kh02_challenge_id::excellent_shoot, next,
                                   next == excellent_shotlock_chain_goal ? kh02_challenge_state::complete
                                                                         : kh02_challenge_state::in_progress);
        return changed;
    }

} // namespace kh3ap::kh02_tracking
