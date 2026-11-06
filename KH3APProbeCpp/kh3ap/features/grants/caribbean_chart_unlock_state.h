#pragma once

#include <chrono>

namespace kh3ap
{
    struct RuntimeCaribbeanChartUnlockState
    {
        enum class Phase
        {
            idle,
            awaiting_map_ready,
            delayed,
            complete,
            failed,
        };

        Phase phase{Phase::idle};
        std::chrono::steady_clock::time_point apply_not_before{};
        int attempts{};

        auto reset() -> void
        {
            phase = Phase::idle;
            apply_not_before = {};
            attempts = 0;
        }

        auto arm() -> bool
        {
            if (phase != Phase::idle)
            {
                return false;
            }
            phase = Phase::awaiting_map_ready;
            return true;
        }

        auto mark_map_ready(
                std::chrono::steady_clock::time_point now,
                std::chrono::steady_clock::duration delay) -> bool
        {
            if (phase != Phase::awaiting_map_ready)
            {
                return false;
            }
            phase = Phase::delayed;
            apply_not_before = now + delay;
            return true;
        }

        [[nodiscard]] auto ready(std::chrono::steady_clock::time_point now) const -> bool
        {
            return phase == Phase::delayed && now >= apply_not_before;
        }

        auto record_attempt(
                bool succeeded,
                std::chrono::steady_clock::time_point now,
                std::chrono::steady_clock::duration retry_delay,
                int max_attempts) -> void
        {
            if (phase != Phase::delayed)
            {
                return;
            }
            ++attempts;
            if (succeeded)
            {
                phase = Phase::complete;
                apply_not_before = {};
                return;
            }
            if (attempts >= max_attempts)
            {
                phase = Phase::failed;
                apply_not_before = {};
                return;
            }
            apply_not_before = now + retry_delay;
        }

        [[nodiscard]] auto pending() const -> bool
        {
            return phase == Phase::awaiting_map_ready || phase == Phase::delayed;
        }
    };
}
