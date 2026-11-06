#include "kh3ap/diagnostics/deferred_event_state_resample_queue.h"

#include <utility>

namespace kh3ap
{
    auto DeferredEventStateResampleQueue::schedule(
            std::wstring source_label,
            std::chrono::steady_clock::duration delay,
            size_t max_pending,
            std::chrono::steady_clock::time_point now)
            -> DeferredEventStateResampleScheduleResult
    {
        DeferredEventStateResampleScheduleResult result{};
        std::lock_guard lock{mutex_};
        if (pending_.size() >= max_pending)
        {
            pending_.erase(pending_.begin());
            result.dropped_oldest = true;
        }

        pending_.emplace_back(DeferredEventStateResample{
                .target_time = now + delay,
                .source_label = std::move(source_label)});
        result.pending_count = pending_.size();
        return result;
    }

    auto DeferredEventStateResampleQueue::pop_due(std::chrono::steady_clock::time_point now)
            -> DeferredEventStateResamplePopResult
    {
        DeferredEventStateResamplePopResult result{};
        std::lock_guard lock{mutex_};
        result.pending_count = pending_.size();
        auto it = pending_.begin();
        while (it != pending_.end())
        {
            const auto ms_remaining = std::chrono::duration_cast<std::chrono::milliseconds>(it->target_time - now).count();
            if (!result.earliest_ms_remaining.has_value() || ms_remaining < *result.earliest_ms_remaining)
            {
                result.earliest_ms_remaining = ms_remaining;
            }
            if (it->target_time <= now)
            {
                result.source_labels.emplace_back(std::move(it->source_label));
                it = pending_.erase(it);
                continue;
            }
            ++it;
        }
        return result;
    }

    auto DeferredEventStateResampleQueue::should_log_pending_wait(
            std::chrono::steady_clock::time_point now,
            std::chrono::steady_clock::duration interval) -> bool
    {
        std::lock_guard lock{mutex_};
        if (last_pending_wait_log_at_.time_since_epoch().count() != 0 &&
            (now - last_pending_wait_log_at_) < interval)
        {
            return false;
        }

        last_pending_wait_log_at_ = now;
        return true;
    }

    auto DeferredEventStateResampleQueue::size() const -> size_t
    {
        std::lock_guard lock{mutex_};
        return pending_.size();
    }
}
