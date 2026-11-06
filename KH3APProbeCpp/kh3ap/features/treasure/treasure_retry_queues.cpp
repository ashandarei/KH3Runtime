#include "treasure_retry_queues.h"
#include "kh3ap/core/objects/uobject_weak.h"
#include <utility>

namespace kh3ap
{
    auto TreasureRetryQueue::schedule(RC::Unreal::UObject* object, std::wstring source_label,
                                      int attempts, std::chrono::steady_clock::duration delay,
                                      size_t max_pending, std::chrono::steady_clock::time_point now)
        -> TreasureRetryScheduleResult
    {
        auto weak_object = make_uobject_weak(object);
        TreasureRetryScheduleResult result{};
        std::lock_guard lock{mutex_};
        if (pending_.size() >= max_pending)
        {
            pending_.erase(pending_.begin());
            result.dropped_oldest = true;
        }
        pending_.push_back(
            {std::move(weak_object), std::move(source_label), attempts, now + delay});
        result.pending_count = pending_.size();
        return result;
    }

    auto TreasureRetryQueue::pop_due(std::chrono::steady_clock::time_point now)
        -> std::vector<DeferredTreasureProbe>
    {
        std::vector<DeferredTreasureProbe> due;
        std::lock_guard lock{mutex_};
        auto entry = pending_.begin();
        while (entry != pending_.end())
        {
            if (entry->target_time <= now)
            {
                due.emplace_back(std::move(*entry));
                entry = pending_.erase(entry);
                continue;
            }
            ++entry;
        }
        return due;
    }

    auto TreasureRetryQueue::size() const -> size_t
    {
        std::lock_guard lock{mutex_};
        return pending_.size();
    }
} // namespace kh3ap
