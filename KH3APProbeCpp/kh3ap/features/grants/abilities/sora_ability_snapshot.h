#pragma once

#include "kh3ap/features/grants/abilities/sora_ability_layout.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace kh3ap
{
    struct RuntimeSoraAbilityEntryState
    {
        bool owned{};
        bool equipped{};
    };

    enum class RuntimeSoraAbilitySnapshotResult
    {
        Read,
        InvalidRange,
        Unavailable,
    };

    // Query a contiguous range in one read. The reader resolves the current
    // Sora bank for this call; neither bank addresses nor ability values are
    // retained between polls. Publish no partial state if the read fails.
    template <typename ReadEntries>
    auto read_runtime_sora_ability_snapshot(uint8_t first_ability,
                                            std::span<RuntimeSoraAbilityEntryState> states,
                                            ReadEntries&& read_entries)
        -> RuntimeSoraAbilitySnapshotResult
    {
        std::fill(states.begin(), states.end(), RuntimeSoraAbilityEntryState{});
        constexpr size_t ability_id_count = sora_ability_layout::entry_count;
        if (first_ability == 0 || states.empty() ||
            states.size() > ability_id_count - first_ability)
        {
            return RuntimeSoraAbilitySnapshotResult::InvalidRange;
        }

        std::array<uint32_t, ability_id_count> entries{};
        const std::span<uint32_t> requested{entries.data(), states.size()};
        if (!read_entries(first_ability, requested))
        {
            return RuntimeSoraAbilitySnapshotResult::Unavailable;
        }
        for (size_t index = 0; index < states.size(); ++index)
        {
            const bool owned = (entries[index] & sora_ability_layout::owned_mask) != 0;
            states[index] = {owned,
                             owned && (entries[index] & sora_ability_layout::equipped_mask) != 0};
        }
        return RuntimeSoraAbilitySnapshotResult::Read;
    }

    // Read a selection from one live range. Keep the caller's order (including
    // duplicate IDs), and publish nothing until the entire range succeeds.
    template <typename ReadRange>
    auto read_runtime_sora_ability_selection(std::span<const uint8_t> abilities,
                                             std::span<RuntimeSoraAbilityEntryState> states,
                                             ReadRange&& read_range)
        -> RuntimeSoraAbilitySnapshotResult
    {
        std::fill(states.begin(), states.end(), RuntimeSoraAbilityEntryState{});
        constexpr size_t ability_id_count = sora_ability_layout::entry_count;
        if (abilities.empty() || abilities.size() != states.size() ||
            abilities.size() > ability_id_count)
        {
            return RuntimeSoraAbilitySnapshotResult::InvalidRange;
        }
        const auto [first, last] = std::minmax_element(abilities.begin(), abilities.end());
        if (*first == 0)
        {
            return RuntimeSoraAbilitySnapshotResult::InvalidRange;
        }
        std::array<RuntimeSoraAbilityEntryState, ability_id_count> snapshot{};
        const size_t count = static_cast<size_t>(*last - *first) + 1;
        if (!read_range(*first, std::span{snapshot.data(), count}))
        {
            return RuntimeSoraAbilitySnapshotResult::Unavailable;
        }
        for (size_t index = 0; index < abilities.size(); ++index)
        {
            states[index] = snapshot[abilities[index] - *first];
        }
        return RuntimeSoraAbilitySnapshotResult::Read;
    }
} // namespace kh3ap
