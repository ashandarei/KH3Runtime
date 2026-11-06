#include "kh3ap/diagnostics/ability_probe/stable_ability_trace_dedupe.h"

#include <utility>

namespace kh3ap
{
    auto StableAbilityTraceDedupe::check_and_remember(std::wstring dedupe_key, size_t max_unique)
            -> StableAbilityTraceDedupeResult
    {
        std::lock_guard lock{mutex_};
        if (seen_.contains(dedupe_key))
        {
            ++suppressed_;
            return StableAbilityTraceDedupeResult{
                    .decision = StableAbilityTraceDecision::Duplicate,
                    .suppressed_count = suppressed_};
        }
        if (seen_.size() >= max_unique)
        {
            ++suppressed_;
            return StableAbilityTraceDedupeResult{
                    .decision = StableAbilityTraceDecision::UniqueLimitReached,
                    .suppressed_count = suppressed_};
        }

        seen_.insert(std::move(dedupe_key));
        return StableAbilityTraceDedupeResult{
                .decision = StableAbilityTraceDecision::Log,
                .suppressed_count = suppressed_};
    }
}
