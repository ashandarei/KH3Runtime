#pragma once
#include "kh02_challenge_policy.h"
#include "kh3ap/features/kh02/progress/kh02_progress_policy.h"
#include <array>
#include <span>
#include <string>
#include <string_view>

namespace kh3ap
{
    // Reconcile source-owned outcomes before publishing a value-only progress
    // snapshot. This function neither reads engine objects nor sends AP data.
    inline void kh02_reconcile_challenges(const kh02_progress::Flags& flags, std::map<int, int>& challenges,
                                          std::map<int, int>& counts)
    {
        constexpr int white_gem_goal = 7;
        const auto wide_flag = [&](std::wstring_view key)
        {
            // These keys come only from the policy's hard-coded ASCII flag
            // identifiers, never from localized or user-entered text.
            std::string identifier;
            identifier.reserve(key.size());
            for (const auto character : key)
            {
                identifier.push_back(static_cast<char>(character));
            }
            return kh02_progress::flag_value(flags, identifier) == 1;
        };
        kh02_tracking::mirror_outcomes(challenges,
                                       [&](const char* key) { return kh02_progress::flag_value(flags, key) == 1; });
        kh02_tracking::forest_outcomes(challenges, counts, wide_flag);
        kh02_tracking::ending_outcomes(challenges, counts, wide_flag);
        const int thorn_goal = kh02_tracking::goal(kh02_challenge_id::destroy_thorn);
        if (counts[kh02_challenge_id::destroy_thorn] >= thorn_goal)
        {
            counts[kh02_challenge_id::destroy_thorn] = thorn_goal;
            challenges[kh02_challenge_id::destroy_thorn] = kh02_challenge_state::complete;
        }
        const int gems = kh02_progress::white_gem_count(flags);
        counts[kh02_challenge_id::get_white_gem_mirror_area] = gems;
        if (gems == white_gem_goal)
        {
            challenges[kh02_challenge_id::get_white_gem_mirror_area] = kh02_challenge_state::complete;
        }
        else if (challenges[kh02_challenge_id::get_white_gem_mirror_area] == kh02_challenge_state::complete)
        {
            challenges[kh02_challenge_id::get_white_gem_mirror_area] = kh02_challenge_state::in_progress;
        }
        constexpr std::array<std::string_view, 5> castle_chests{"DW_LBOX01", "DW_BOX003", "DW_BOX004", "DW_BOX006",
                                                                "DW_BOX007"};
        constexpr std::array<std::string_view, 8> mirror_chests{"DW_BOX008", "DW_LBOX02", "DW_BOX009", "DW_BOX011",
                                                                "DW_BOX013", "DW_BOX033", "DW_BOX015", "DW_BOX016"};
        const auto reconcile_chests = [&](int id, std::span<const std::string_view> chest_ids)
        {
            int opened = 0;
            for (const auto chest : chest_ids)
            {
                opened += kh02_progress::flag_value(flags, "gameflow_DW_CHALLENGE_CHEST_" + std::string(chest)) == 1;
            }
            counts[id] = opened;
            challenges[id] = opened == static_cast<int>(chest_ids.size()) ? kh02_challenge_state::complete
                                                                          : kh02_challenge_state::in_progress;
        };
        reconcile_chests(kh02_challenge_id::get_all_treasure_gear_area, castle_chests);
        reconcile_chests(kh02_challenge_id::get_all_treasure_mirror_area, mirror_chests);
    }
} // namespace kh3ap
