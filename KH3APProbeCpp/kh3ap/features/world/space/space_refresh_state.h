#pragma once

#include <chrono>

namespace kh3ap::space
{
    inline constexpr auto context_refresh_timeout = std::chrono::seconds{30};

    // Coalesce events on the game thread. An unsafe/loading context defers
    // only the requested work; a successful refresh leaves no periodic work.
    class RefreshRequest
    {
    public:
        using Clock = std::chrono::steady_clock;
        static constexpr auto retry_delay = std::chrono::milliseconds{250};

        void request()
        {
            pending_ = true;
            next_ = {};
        }

        void clear()
        {
            pending_ = false;
            next_ = {};
        }

        bool take(Clock::time_point now)
        {
            if (!pending_ || now < next_)
            {
                return false;
            }
            pending_ = false;
            return true;
        }
        void finish(Clock::time_point now, bool complete)
        {
            // A native refresh can itself emit a newer lifecycle event. Never
            // erase that request when completing the work we just consumed.
            if (!complete && !pending_)
            {
                pending_ = true;
                next_ = now + retry_delay;
            }
        }

    private:
        bool pending_{};
        Clock::time_point next_{};
    };
}
