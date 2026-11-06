#pragma once
#include <cmath>
#include <cstdint>

namespace kh3ap
{
    inline constexpr double frame_wait_busy_tail_seconds = 0.0005;
    inline constexpr double frame_wait_minimum_sleep_seconds = 0.0001;
    inline constexpr double frame_wait_maximum_seconds = 0.010;
    inline constexpr double windows_due_time_ticks_per_second = 10000000.0;

    // Leave the final half millisecond to the game's original deadline loop.
    // Only the short, verified frame-limiter wait is eligible for replacement.
    inline auto frame_wait_due_time(double remaining_seconds) noexcept -> int64_t
    {
        if (!std::isfinite(remaining_seconds) ||
            remaining_seconds <= frame_wait_busy_tail_seconds + frame_wait_minimum_sleep_seconds ||
            remaining_seconds > frame_wait_maximum_seconds)
        {
            return 0;
        }
        return -static_cast<int64_t>((remaining_seconds - frame_wait_busy_tail_seconds) *
                                     windows_due_time_ticks_per_second);
    }
} // namespace kh3ap
