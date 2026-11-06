#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace kh3ap
{
    struct DeferredEventStateResample
    {
        std::chrono::steady_clock::time_point target_time{};
        std::wstring source_label{};
    };

    struct DeferredEventStateResampleScheduleResult
    {
        bool dropped_oldest{false};
        size_t pending_count{0};
    };

    struct DeferredEventStateResamplePopResult
    {
        std::vector<std::wstring> source_labels{};
        size_t pending_count{0};
        std::optional<int64_t> earliest_ms_remaining{};
    };

    class DeferredEventStateResampleQueue
    {
    public:
        static constexpr size_t max_pending = 64;

        auto schedule(
                std::wstring source_label,
                std::chrono::steady_clock::duration delay,
                size_t max_pending,
                std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now())
                -> DeferredEventStateResampleScheduleResult;

        auto pop_due(std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now())
                -> DeferredEventStateResamplePopResult;

        auto should_log_pending_wait(
                std::chrono::steady_clock::time_point now,
                std::chrono::steady_clock::duration interval) -> bool;

        [[nodiscard]] auto size() const -> size_t;

    private:
        mutable std::mutex mutex_{};
        std::vector<DeferredEventStateResample> pending_{};
        std::chrono::steady_clock::time_point last_pending_wait_log_at_{};
    };
}
