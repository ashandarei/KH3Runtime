#include "kh3ap/features/grants/delivery/manual_runtime_item_grant_state.h"

namespace kh3ap
{
    auto ManualRuntimeItemGrantState::arm(std::chrono::steady_clock::time_point now) -> void
    {
        armed_ = true;
        armed_at_ = now;
    }

    auto ManualRuntimeItemGrantState::consume_if_ready(
            std::chrono::steady_clock::duration delay,
            std::chrono::steady_clock::time_point now) -> bool
    {
        if (!armed_ || armed_at_.time_since_epoch().count() == 0 || (now - armed_at_) < delay)
        {
            return false;
        }

        armed_ = false;
        return true;
    }

    auto ManualRuntimeItemGrantState::within_arm_grace(
            std::chrono::steady_clock::duration grace,
            std::chrono::steady_clock::time_point now) const -> bool
    {
        return armed_at_.time_since_epoch().count() != 0 && (now - armed_at_) < grace;
    }

    auto ManualRuntimeItemGrantState::can_attempt_fallback(
            std::chrono::steady_clock::duration throttle,
            std::chrono::steady_clock::time_point now) const -> bool
    {
        return last_fallback_attempt_at_.time_since_epoch().count() == 0 ||
               (now - last_fallback_attempt_at_) >= throttle;
    }

    auto ManualRuntimeItemGrantState::mark_fallback_attempt(std::chrono::steady_clock::time_point now) -> void
    {
        last_fallback_attempt_at_ = now;
    }
}
