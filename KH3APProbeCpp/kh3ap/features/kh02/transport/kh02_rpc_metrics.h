#pragma once

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <mutex>

namespace kh02asset
{
    // Bounded cumulative measurements. Histogram percentiles report bucket
    // upper bounds; they are not exact latency percentiles or frame timings.
    inline constexpr std::array<std::uint64_t, 9> rpc_latency_bounds_us{10,    50,     100,     500,    1'000,
                                                                        5'000, 20'000, 100'000, 500'000};
    inline constexpr auto rpc_latency_bucket_count = rpc_latency_bounds_us.size() + 1;

    struct RpcTimingTotals
    {
        std::uint64_t attempts{}, successful{};
        std::uint64_t queue_wait_us{}, maximum_queue_wait_us{};
        std::uint64_t service_us{}, maximum_service_us{};
        std::array<std::uint64_t, rpc_latency_bucket_count> queue_histogram{}, service_histogram{};
    };

    class RpcTimingMetrics
    {
      public:
        void record(std::uint64_t queue_wait_us, std::uint64_t service_us, bool successful)
        {
            std::lock_guard lock(mutex_);
            ++totals_.attempts;
            totals_.successful += successful;
            totals_.queue_wait_us += queue_wait_us;
            totals_.maximum_queue_wait_us = std::max(totals_.maximum_queue_wait_us, queue_wait_us);
            totals_.service_us += service_us;
            totals_.maximum_service_us = std::max(totals_.maximum_service_us, service_us);
            ++totals_.queue_histogram[bucket(queue_wait_us)];
            ++totals_.service_histogram[bucket(service_us)];
        }

        auto snapshot() const -> RpcTimingTotals
        {
            std::lock_guard lock(mutex_);
            return totals_;
        }

      private:
        static auto bucket(std::uint64_t duration_us) -> std::size_t
        {
            return std::lower_bound(rpc_latency_bounds_us.begin(), rpc_latency_bounds_us.end(), duration_us) -
                   rpc_latency_bounds_us.begin();
        }

        mutable std::mutex mutex_;
        RpcTimingTotals totals_;
    };

    class RpcTimingObservation
    {
      public:
        explicit RpcTimingObservation(RpcTimingMetrics& metrics)
            : metrics_(metrics), began_(Clock::now()), acquired_(began_)
        {
        }

        void lock_acquired()
        {
            acquired_ = Clock::now();
        }

        void succeeded()
        {
            successful_ = true;
        }

        ~RpcTimingObservation()
        {
            const auto finished = Clock::now();
            metrics_.record(microseconds(acquired_ - began_), microseconds(finished - acquired_), successful_);
        }

        RpcTimingObservation(const RpcTimingObservation&) = delete;
        auto operator=(const RpcTimingObservation&) -> RpcTimingObservation& = delete;

      private:
        using Clock = std::chrono::steady_clock;

        static auto microseconds(Clock::duration duration) -> std::uint64_t
        {
            return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(duration).count());
        }

        RpcTimingMetrics& metrics_;
        Clock::time_point began_, acquired_;
        bool successful_{};
    };
} // namespace kh02asset
