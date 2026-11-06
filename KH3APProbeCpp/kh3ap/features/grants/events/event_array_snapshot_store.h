#pragma once

#include <cstdint>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace kh3ap
{
    struct EventArraySnapshotEmission
    {
        std::wstring snapshot_key{};
        std::vector<std::wstring> added{};
        std::vector<std::wstring> removed{};
        bool warm_cache{false};
        size_t seeded_count{0};
    };

    class EventArraySnapshotStore
    {
    public:
        auto begin_pass() -> void;
        auto accumulate(std::wstring snapshot_key, const std::vector<std::wstring>& entries) -> void;
        auto finalize_pass() -> std::vector<EventArraySnapshotEmission>;

    private:
        struct State
        {
            std::unordered_set<std::wstring> seen_entries{};
            std::unordered_set<std::wstring> pending_entries{};
            uint64_t pending_generation{0};
            bool warm_cache_populated{false};
        };

        std::mutex mutex_{};
        std::unordered_map<std::wstring, State> state_by_key_{};
        uint64_t generation_{0};
    };
}
