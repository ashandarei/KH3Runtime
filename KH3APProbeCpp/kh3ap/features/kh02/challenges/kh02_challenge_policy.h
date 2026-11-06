#pragma once
// Stable umbrella for runtime callers; each pure domain owns its own rules.
#include "kh02_challenge_progress_rules.h"
#include "kh02_challenge_combat_rules.h"
#include "kh02_challenge_encounter_sites.h"
#include "kh3ap/features/kh02/treasure/kh02_chest_journal.h"
#include <cmath>
#include <set>
#include <cstdint>
#include <vector>
#include <string>

namespace kh3ap::kh02_tracking
{
    inline constexpr int freeze_cohort_limit = 128;
    inline constexpr int freeze_cohort_minimum_enemies = 5;
    inline constexpr double maximum_ice_slide_sample_distance_cm = 1'500;

    struct FreezeCohorts
    {
        struct Cohort
        {
            std::set<uint64_t> members, broken;
        };

        std::vector<Cohort> groups;

        void observe(const std::set<uint64_t>& frozen)
        {
            std::erase_if(groups,
                          [&](const Cohort& group)
                          {
                              size_t possible = group.broken.size();
                              for (auto id : group.members)
                              {
                                  possible += !group.broken.contains(id) && frozen.contains(id);
                              }
                              return possible < 5;
                          });
            if (frozen.size() < freeze_cohort_minimum_enemies || groups.size() >= freeze_cohort_limit)
            {
                return;
            }
            for (const auto& group : groups)
            {
                if (group.members == frozen)
                {
                    return;
                }
            }
            groups.push_back({frozen, {}});
        }

        int shatter(uint64_t identity)
        {
            int best = 0;
            for (auto& group : groups)
            {
                if (group.members.contains(identity))
                {
                    group.broken.insert(identity);
                    best = std::max(best, std::min(5, static_cast<int>(group.broken.size())));
                }
            }
            return best;
        }
    };

    // The source adds travelled distance to its ice-rail statistic. Keep that
    // progress across rail segments; ending a state only drops the last position.
    struct IceSlide
    {
        bool active{};
        double centimeters{};
        std::array<float, 3> previous{};
        bool positioned{};

        void reset()
        {
            *this = {};
        }

        void end()
        {
            active = positioned = false;
        }

        int sample(std::array<float, 3> position)
        {
            if (!active)
            {
                return 0;
            }
            if (positioned)
            {
                double squared = 0;
                for (int axis = 0; axis < 3; ++axis)
                {
                    const double delta = position[axis] - previous[axis];
                    squared += delta * delta;
                }
                const double distance = std::sqrt(squared);
                if (!std::isfinite(distance) || distance > maximum_ice_slide_sample_distance_cm)
                {
                    end();
                    return std::min(ice_slide_goal_meters, static_cast<int>(centimeters / centimeters_per_meter));
                }
                centimeters += distance;
            }
            previous = position;
            positioned = true;
            return std::min(ice_slide_goal_meters, static_cast<int>(centimeters / centimeters_per_meter));
        }
    };

    using kh02_chest_journal::forest_chests;

    inline constexpr std::array<std::wstring_view, 5> depths_chests{L"DW_BOX026", L"DW_BOX027", L"DW_BOX028",
                                                                    L"DW_BOX029", L"DW_LBOX04"};

    inline constexpr std::array<std::wstring_view, 12> zodiac_chests{
        L"DW_BOX002", L"DW_BOX005", L"DW_BOX010", L"DW_BOX012", L"DW_BOX014", L"DW_BOX030",
        L"DW_BOX031", L"DW_BOX032", L"DW_BOX034", L"DW_BOX035", L"DW_BOX036", L"DW_BOX037"};

    using kh02_chest_journal::additional_chests;

    template <class Flag> bool ending_outcomes(std::map<int, int>& states, std::map<int, int>& counts, Flag flag)
    {
        bool changed = false;
        auto reconcile = [&](int id, const auto& identities)
        {
            int value = 0;
            for (auto identity : identities)
            {
                value += flag(std::wstring(L"gameflow_DW_CHALLENGE_CHEST_") + std::wstring(identity));
            }
            const int next = value == static_cast<int>(identities.size()) ? kh02_challenge_state::complete
                                                                          : kh02_challenge_state::in_progress;
            if (counts[id] != value || states[id] != next)
            {
                progress_mutation::counted(states, counts, id, value, next);
                changed = true;
            }
        };
        reconcile(kh02_challenge_id::get_all_star_sign, zodiac_chests);
        reconcile(kh02_challenge_id::get_all_treasure_dark_area, depths_chests);
        if (flag(L"gameflow_CHALLENGE_49_dw_09_MEMORY"))
        {
            changed |= complete(states, kh02_challenge_id::get_secret_dark_area);
        }
        return changed;
    }

    template <class Flag> bool forest_outcomes(std::map<int, int>& states, std::map<int, int>& counts, Flag flag)
    {
        bool changed = false;
        auto reconcile = [&](int id, int value)
        {
            const int next = value >= goal(id) ? kh02_challenge_state::complete : kh02_challenge_state::in_progress;
            if (counts[id] != value || states[id] != next)
            {
                progress_mutation::counted(states, counts, id, value, next);
                changed = true;
            }
        };
        int flowers = 0;
        for (const auto* key : {L"gameflow_CHALLENGE_34_RED_FLOWER", L"gameflow_CHALLENGE_34_BLUE_FLOWER",
                                L"gameflow_CHALLENGE_34_GREEN_FLOWER"})
        {
            flowers += flag(key);
        }
        reconcile(kh02_challenge_id::check_all_flower, flowers);
        int opened = 0;
        for (const auto id : forest_chests)
        {
            opened += flag((std::wstring(L"gameflow_DW_CHALLENGE_CHEST_") + std::wstring(id)).c_str());
        }
        reconcile(kh02_challenge_id::get_all_treasure_thorn_area, opened);
        if (flag(L"gameflow_CHALLENGE_45_dw_08_MEMORY") &&
            states[kh02_challenge_id::get_secret_thorn_area] != kh02_challenge_state::complete)
        {
            progress_mutation::state(states, kh02_challenge_id::get_secret_thorn_area, kh02_challenge_state::complete);
            changed = true;
        }
        return changed;
    }

    template <class Flag> bool mirror_outcomes(std::map<int, int>& states, Flag flag)
    {
        bool changed = false;
        // These flags are written by the original room's actual win/pickup path.
        // In particular, a Phantom defeat flag cannot prove a no-damage victory.
        for (const auto [id, key] :
             {std::pair{kh02_challenge_id::all_kill_mirror_area, "gameflow_DW_MIRROR_BATTLE01_CLEAR"},
              std::pair{kh02_challenge_id::dark_room_battle, "gameflow_DW_MIRROR_BATTLE03_CLEAR"},
              std::pair{kh02_challenge_id::open_true_treasure_mirror_area, "gameflow_DW_MIRROR_TREASURE_CLEAR"},
              std::pair{kh02_challenge_id::get_secret_mirror_area, "gameflow_CHALLENGE_40_dw_05_MEMORY"}})
        {
            if (flag(key) && states[id] != kh02_challenge_state::complete)
            {
                progress_mutation::state(states, id, kh02_challenge_state::complete);
                changed = true;
            }
        }
        return changed;
    }
} // namespace kh3ap::kh02_tracking
