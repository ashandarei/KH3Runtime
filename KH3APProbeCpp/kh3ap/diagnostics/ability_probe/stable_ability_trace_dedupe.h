#pragma once

#include <cstdint>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_set>

namespace kh3ap
{
    enum class StableAbilityTraceDecision
    {
        Log,
        Duplicate,
        UniqueLimitReached,
    };

    struct StableAbilityTraceDedupeResult
    {
        StableAbilityTraceDecision decision{StableAbilityTraceDecision::Log};
        uint64_t suppressed_count{0};
    };

    class StableAbilityTraceDedupe
    {
    public:
        static constexpr size_t max_unique_entries = 512;

        auto check_and_remember(std::wstring dedupe_key, size_t max_unique) -> StableAbilityTraceDedupeResult;

    private:
        std::mutex mutex_{};
        std::unordered_set<std::wstring> seen_{};
        uint64_t suppressed_{0};
    };
}
