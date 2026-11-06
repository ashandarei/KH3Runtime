#pragma once

#include <chrono>

namespace kh3ap
{
    class ManualRuntimeItemGrantState
    {
    public:
        auto arm(std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now()) -> void;
        auto consume_if_ready(
                std::chrono::steady_clock::duration delay,
                std::chrono::steady_clock::time_point now) -> bool;
        [[nodiscard]] auto within_arm_grace(
                std::chrono::steady_clock::duration grace,
                std::chrono::steady_clock::time_point now) const -> bool;

        [[nodiscard]] auto can_attempt_fallback(
                std::chrono::steady_clock::duration throttle,
                std::chrono::steady_clock::time_point now) const -> bool;
        auto mark_fallback_attempt(std::chrono::steady_clock::time_point now) -> void;

    private:
        bool armed_{false};
        std::chrono::steady_clock::time_point armed_at_{};
        std::chrono::steady_clock::time_point last_fallback_attempt_at_{};
    };
}
