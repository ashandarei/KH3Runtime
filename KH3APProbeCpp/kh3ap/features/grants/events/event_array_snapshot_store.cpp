#include "kh3ap/features/grants/events/event_array_snapshot_store.h"

#include <utility>

namespace kh3ap
{
    auto EventArraySnapshotStore::begin_pass() -> void
    {
        std::lock_guard lock{mutex_};
        ++generation_;
    }

    auto EventArraySnapshotStore::accumulate(std::wstring snapshot_key, const std::vector<std::wstring>& entries) -> void
    {
        std::lock_guard lock{mutex_};
        auto& state = state_by_key_[std::move(snapshot_key)];
        if (state.pending_generation != generation_)
        {
            state.pending_entries.clear();
            state.pending_generation = generation_;
        }
        for (const auto& entry : entries)
        {
            state.pending_entries.insert(entry);
        }
    }

    auto EventArraySnapshotStore::finalize_pass() -> std::vector<EventArraySnapshotEmission>
    {
        std::vector<EventArraySnapshotEmission> emissions{};
        std::lock_guard lock{mutex_};
        const uint64_t current_generation = generation_;
        for (auto& [snapshot_key, state] : state_by_key_)
        {
            if (state.pending_generation != current_generation)
            {
                continue;
            }

            EventArraySnapshotEmission emission{};
            emission.snapshot_key = snapshot_key;

            if (!state.warm_cache_populated)
            {
                state.seen_entries = state.pending_entries;
                state.warm_cache_populated = true;
                emission.warm_cache = true;
                emission.seeded_count = state.seen_entries.size();
                emissions.emplace_back(std::move(emission));
                continue;
            }

            for (const auto& entry : state.pending_entries)
            {
                if (!state.seen_entries.contains(entry))
                {
                    emission.added.emplace_back(entry);
                }
            }
            for (const auto& entry : state.seen_entries)
            {
                if (!state.pending_entries.contains(entry))
                {
                    emission.removed.emplace_back(entry);
                }
            }

            state.seen_entries = state.pending_entries;

            if (!emission.added.empty() || !emission.removed.empty())
            {
                emissions.emplace_back(std::move(emission));
            }
        }
        return emissions;
    }
}
