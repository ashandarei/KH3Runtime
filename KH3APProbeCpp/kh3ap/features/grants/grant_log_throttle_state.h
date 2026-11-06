#pragma once

#include <chrono>

namespace kh3ap
{
    class RuntimeGrantLogThrottleState
    {
    public:
        auto mark_runtime_item_grant_log(
                std::chrono::steady_clock::time_point now,
                std::chrono::steady_clock::duration interval) -> bool;
        auto mark_runtime_item_grant_poll_log(
                std::chrono::steady_clock::time_point now,
                std::chrono::steady_clock::duration interval) -> bool;
        auto mark_runtime_item_grant_armed_wait_log(
                std::chrono::steady_clock::time_point now,
                std::chrono::steady_clock::duration interval) -> bool;
        auto mark_manual_runtime_item_grant_log(
                std::chrono::steady_clock::time_point now,
                std::chrono::steady_clock::duration interval) -> bool;

    private:
        static auto mark_if_elapsed(
                std::chrono::steady_clock::time_point& last_log_at,
                std::chrono::steady_clock::time_point now,
                std::chrono::steady_clock::duration interval) -> bool;

        std::chrono::steady_clock::time_point last_runtime_item_grant_log_{};
        std::chrono::steady_clock::time_point last_runtime_item_grant_poll_log_{};
        std::chrono::steady_clock::time_point last_runtime_item_grant_armed_wait_log_{};
        std::chrono::steady_clock::time_point last_manual_runtime_item_grant_log_{};
    };
}
