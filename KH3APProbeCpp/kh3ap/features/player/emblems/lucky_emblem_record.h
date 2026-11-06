#pragma once

#include <array>
#include <cstdint>
#include <limits>
#include <set>
#include <stdexcept>
#include <string>
#include <span>
#include <vector>
#include <algorithm>
#include "kh3ap/hooks/lucky_marks/lucky_mark_retirement.h"

namespace kh3ap::emblem_record
{
    // These are TresLuckyMarkData.MarkIndex values, NOT row names/global IDs.
    // The shipped table is a permutation of 1..90; the native bank has 96 bits.
    inline constexpr int emblem_count = 90;
    inline constexpr int maximum_reward_index = 90;
    inline constexpr int native_word_count = 3;
    inline constexpr int bits_per_word = std::numeric_limits<uint32_t>::digits;
    inline constexpr int last_word_valid_bits =
        emblem_count - (native_word_count - 1) * bits_per_word;
    static_assert(last_word_valid_bits > 0 && last_word_valid_bits <= bits_per_word);
    using Words = std::array<uint32_t, native_word_count>;
    inline constexpr uint32_t last_word_mask = std::numeric_limits<uint32_t>::max() >>
                                               (bits_per_word - last_word_valid_bits);

    // Restore only committed milestones, never infer claims from the photo total.
    // Rows beyond the native 32-bit capacity use the existing retired alias.
    template<class Row>
    auto claimed_rewards(std::span<const int32_t> completed, const std::vector<Row>& rows) -> uint32_t
    {
        uint32_t result{};
        for (const auto& row : rows)
        {
            if (std::find(completed.begin(), completed.end(), row.milestone) == completed.end()) continue;
            if (row.reward_index < 0 || row.reward_index > maximum_reward_index)
            {
                throw std::runtime_error("emblem_reward_index");
            }
            const auto index = lucky_mark_reward_index_needs_retirement(row.reward_index)
                ? lucky_mark_native_retired_alias_index : row.reward_index;
            result |= uint32_t{1} << index;
        }
        return result;
    }

    inline auto from_checkpoint(const std::set<std::wstring>& completed) -> Words
    {
        Words result{};
        for (const auto& id : completed)
        {
            int index{};
            if (id.empty() || id.size() > 2 || id.front() == L'0')
                throw std::runtime_error("emblem_checkpoint_index");
            for (const auto ch : id)
            {
                if (ch < L'0' || ch > L'9') throw std::runtime_error("emblem_checkpoint_index");
                index = index * 10 + ch - L'0';
            }
            if (index < 1 || index > emblem_count)
            {
                throw std::runtime_error("emblem_checkpoint_index");
            }
            --index;
            result[index / bits_per_word] |= uint32_t{1} << (index % bits_per_word);
        }
        return result;
    }

    inline auto checkpoint_ids(const Words& words) -> std::set<std::wstring>
    {
        std::set<std::wstring> ids;
        for (int index = 0; index < emblem_count; ++index)
        {
            if ((words[index / bits_per_word] & (uint32_t{1} << (index % bits_per_word))) != 0)
            {
                ids.insert(std::to_wstring(index + 1));
            }
        }
        return ids;
    }

    inline auto merge(const Words& remembered, const Words& live) -> Words
    {
        return {remembered[0] | live[0], remembered[1] | live[1],
                (remembered[2] | live[2]) & last_word_mask};
    }
}
