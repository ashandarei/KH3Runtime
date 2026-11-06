#include "kh3ap/features/grants/grant_log_throttle_state.h"

namespace kh3ap
{
    auto RuntimeGrantLogThrottleState::mark_runtime_item_grant_log(
            std::chrono::steady_clock::time_point now,
            std::chrono::steady_clock::duration interval) -> bool
    {
        return mark_if_elapsed(last_runtime_item_grant_log_, now, interval);
    }

    auto RuntimeGrantLogThrottleState::mark_runtime_item_grant_poll_log(
            std::chrono::steady_clock::time_point now,
            std::chrono::steady_clock::duration interval) -> bool
    {
        return mark_if_elapsed(last_runtime_item_grant_poll_log_, now, interval);
    }

    auto RuntimeGrantLogThrottleState::mark_runtime_item_grant_armed_wait_log(
            std::chrono::steady_clock::time_point now,
            std::chrono::steady_clock::duration interval) -> bool
    {
        return mark_if_elapsed(last_runtime_item_grant_armed_wait_log_, now, interval);
    }

    auto RuntimeGrantLogThrottleState::mark_manual_runtime_item_grant_log(
            std::chrono::steady_clock::time_point now,
            std::chrono::steady_clock::duration interval) -> bool
    {
        return mark_if_elapsed(last_manual_runtime_item_grant_log_, now, interval);
    }

    auto RuntimeGrantLogThrottleState::mark_if_elapsed(
            std::chrono::steady_clock::time_point& last_log_at,
            std::chrono::steady_clock::time_point now,
            std::chrono::steady_clock::duration interval) -> bool
    {
        if (last_log_at.time_since_epoch().count() != 0 && (now - last_log_at) < interval)
        {
            return false;
        }

        last_log_at = now;
        return true;
    }
}
