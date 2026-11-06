#pragma once

#include <chrono>
#include <cstdint>

namespace kh3ap
{
    inline constexpr auto mom_focus_launch_timeout = std::chrono::seconds(60);
    inline constexpr auto kh3_focus_return_timeout = std::chrono::seconds(10);

    // The window APIs may complete asynchronously or reject a request. Observe the
    // next window event before declaring success; bound retries and never reclaim focus
    // after a successful handoff followed by a deliberate Alt-Tab.
    class RuntimeMoMFocusHandoff
    {
    public:
        using Clock = std::chrono::steady_clock;
        enum class Target
        {
            none,
            mom,
            kh3
        };

        auto observe(uint32_t pid, bool mom_ready, bool mom_foreground, bool kh3_ready,
                     bool kh3_foreground, Clock::time_point now) -> Target
        {
            if (pid != mom_pid_)
            {
                const bool was_running = mom_pid_ != 0;
                mom_pid_ = pid;
                pending_ = pid ? Target::mom : was_running ? Target::kh3 : Target::none;
                deadline_ = now + (pid ? mom_focus_launch_timeout : kh3_focus_return_timeout);
            }
            if (pending_ == Target::none)
            {
                return Target::none;
            }
            if (now >= deadline_ || (pending_ == Target::mom ? mom_foreground : kh3_foreground))
            {
                pending_ = Target::none;
                return Target::none;
            }
            return (pending_ == Target::mom ? mom_ready : kh3_ready) ? pending_ : Target::none;
        }

    private:
        uint32_t mom_pid_{};
        Target pending_{Target::none};
        Clock::time_point deadline_{};
    };
} // namespace kh3ap
