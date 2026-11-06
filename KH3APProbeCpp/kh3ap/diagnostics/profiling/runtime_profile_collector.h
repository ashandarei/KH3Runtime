#pragma once

#include "kh3ap/diagnostics/profiling/profile_key.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace kh3ap
{
    struct RuntimeProfileReport
    {
        std::wstring scope_name;
        uint64_t count{};
        uint64_t total_us{};
        uint64_t max_us{};
        uint64_t last_us{};
        int64_t interval_ms{};
    };

    // Owns statistics only. Callers emit returned reports outside the lock.
    class RuntimeProfileCollector
    {
    public:
        using Clock = std::chrono::steady_clock;

        RuntimeProfileCollector(
                uint64_t slow_threshold_us,
                Clock::duration slow_log_interval,
                Clock::duration report_interval,
                Clock::time_point now = Clock::now())
            : slow_threshold_us_(slow_threshold_us),
              slow_log_interval_(slow_log_interval),
              report_interval_(report_interval),
              next_flush_at_((now + report_interval).time_since_epoch().count())
        {
        }

        // Returns whether this sample needs an immediate slow-call warning.
        auto record(std::wstring_view scope_name, uint64_t elapsed_us,
                    Clock::time_point now) -> bool
        {
            std::lock_guard lock{mutex_};
            auto found = buckets_.find(scope_name);
            if (found == buckets_.end())
                found = buckets_.try_emplace(std::wstring{scope_name}).first;
            auto& bucket = found->second;
            if (bucket.count == 0)
                bucket.interval_start = now;
            ++bucket.count;
            bucket.total_us += elapsed_us;
            bucket.max_us = std::max(bucket.max_us, elapsed_us);
            bucket.last_us = elapsed_us;

            if (elapsed_us < slow_threshold_us_ ||
                (bucket.last_slow_log && now - *bucket.last_slow_log < slow_log_interval_))
                return false;
            bucket.last_slow_log = now;
            return true;
        }

        auto flush_due(Clock::time_point now) -> std::vector<RuntimeProfileReport>
        {
            // Ordinary updates do not lock or walk the scope registry.
            const auto ticks = now.time_since_epoch().count();
            if (ticks < next_flush_at_.load(std::memory_order_relaxed))
                return {};

            std::lock_guard lock{mutex_};
            if (ticks < next_flush_at_.load(std::memory_order_relaxed))
                return {};
            std::vector<RuntimeProfileReport> reports;
            reports.reserve(buckets_.size());
            for (auto& [name, bucket] : buckets_)
            {
                if (bucket.count == 0)
                    continue;
                reports.push_back({name, bucket.count, bucket.total_us,
                                   bucket.max_us, bucket.last_us,
                                   std::chrono::duration_cast<std::chrono::milliseconds>(
                                           now - bucket.interval_start).count()});
                bucket.count = 0;
                bucket.total_us = 0;
                bucket.max_us = 0;
            }
            next_flush_at_.store((now + report_interval_).time_since_epoch().count(),
                                 std::memory_order_relaxed);
            return reports;
        }

    private:
        struct Bucket
        {
            uint64_t count{};
            uint64_t total_us{};
            uint64_t max_us{};
            uint64_t last_us{};
            Clock::time_point interval_start{};
            std::optional<Clock::time_point> last_slow_log;
        };

        uint64_t slow_threshold_us_;
        Clock::duration slow_log_interval_;
        Clock::duration report_interval_;
        std::atomic<Clock::duration::rep> next_flush_at_;
        std::mutex mutex_;
        std::unordered_map<std::wstring, Bucket, RuntimeProfileKeyHash, std::equal_to<>> buckets_;
    };
}
