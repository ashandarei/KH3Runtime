#pragma once
#include <array>
#include <algorithm>
#include <cstdint>
#include <optional>
#include <span>

namespace kh3ap
{
    // Counts are a single synchronous snapshot. A failed/partial read exposes no
    // values and never reaches native command-list mutations.
    template <size_t Count, class Read>
    auto read_command_bonus_counts(uint8_t first, uint8_t last, Read&& read)
        -> std::optional<std::array<uint8_t, Count>>
    {
        static_assert(Count > 0 && Count <= 256);
        if (first == 0 || first > last || Count > static_cast<size_t>(last - first) + 1)
        {
            return {};
        }
        std::array<uint8_t, Count> counts{};
        if (!read(first, std::span<uint8_t>{counts}))
        {
            return {};
        }
        return counts;
    }

    // Read ownership before preparing a native manager. Missing commands need no
    // setup, while failed inventory reads remain failures, never "nothing owned".
    template <size_t Count, class Query, class Prepare, class Apply>
    auto refresh_owned_commands(Query&& query, Prepare&& prepare, Apply&& apply) -> bool
    {
        std::array<bool, Count> owned{};
        for (size_t index = 0; index < Count; ++index)
        {
            if (!query(index, owned[index]))
            {
                return false;
            }
        }
        if (std::none_of(owned.begin(), owned.end(),
                         [](bool value)
                         {
                             return value;
                         }))
        {
            return true;
        }
        if (!prepare())
        {
            return false;
        }
        for (size_t index = 0; index < Count; ++index)
        {
            if (owned[index] && !apply(index))
            {
                return false;
            }
        }
        return true;
    }
} // namespace kh3ap
