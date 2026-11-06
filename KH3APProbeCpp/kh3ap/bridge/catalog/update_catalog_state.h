#pragma once

#include <chrono>
#include <cstdint>

namespace kh3ap
{
    auto has_elapsed_since(
            std::chrono::steady_clock::time_point start,
            std::chrono::steady_clock::time_point now,
            std::chrono::steady_clock::duration delay) -> bool;

    auto should_run_periodic_update(
            std::chrono::steady_clock::time_point& last_run_at,
            std::chrono::steady_clock::time_point now,
            std::chrono::steady_clock::duration interval) -> bool;

    class UpdateHeartbeatState
    {
    public:
        auto increment_calls() -> uint64_t;
        [[nodiscard]] auto calls() const -> uint64_t;
        auto should_check(std::chrono::steady_clock::time_point now, std::chrono::steady_clock::duration interval) -> bool;
        auto should_log(std::chrono::steady_clock::time_point now, std::chrono::steady_clock::duration interval) -> bool;

    private:
        uint64_t calls_{0};
        std::chrono::steady_clock::time_point last_check_at_{};
        std::chrono::steady_clock::time_point last_log_at_{};
    };

}
