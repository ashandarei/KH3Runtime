#pragma once
#include <chrono>
#include <string_view>

namespace kh3ap
{
    // Recovery only schedules safe native travel and a single arrival notice.
    // It never changes story progress or releases a failed-session latch.
    class Kh02RecoveryState
    {
      public:
        using Clock = std::chrono::steady_clock;
        static constexpr auto return_timeout = std::chrono::seconds(15);
        static constexpr auto notification_timeout = std::chrono::seconds(30);
        enum class Phase
        {
            healthy,
            failed,
            exiting,
            returned
        };
        enum class Action
        {
            none,
            attempt,
            timed_out
        };

        auto begin(Clock::time_point now) -> bool
        {
            if (phase_ != Phase::healthy)
            {
                return false;
            }
            phase_ = Phase::failed;
            request_return(now);
            return true;
        }

        void request_return(Clock::time_point now)
        {
            if (phase_ == Phase::failed && !return_pending_)
            {
                return_pending_ = true;
                return_deadline_ = now + return_timeout;
            }
        }

        auto poll_return(Clock::time_point now) -> Action
        {
            if (!return_pending_)
            {
                return Action::none;
            }
            if (now >= return_deadline_)
            {
                return_pending_ = false;
                return Action::timed_out;
            }
            return Action::attempt;
        }

        void return_dispatched(Clock::time_point now)
        {
            if (phase_ != Phase::failed || !return_pending_)
            {
                return;
            }
            return_pending_ = false;
            phase_ = Phase::exiting;
            notification_pending_ = true;
            notification_deadline_ = now + notification_timeout;
        }

        void return_failed()
        {
            return_pending_ = false;
        }

        auto poll_notification(Clock::time_point now, bool garden_ready) -> Action
        {
            if (!notification_pending_)
            {
                return Action::none;
            }
            if (now >= notification_deadline_)
            {
                notification_pending_ = false;
                return Action::timed_out;
            }
            if (!garden_ready)
            {
                return Action::none;
            }
            phase_ = Phase::returned;
            notification_pending_ = false;
            notification_attempted_ = true;
            return Action::attempt;
        }

        void notification_shown()
        {
            notification_shown_ = notification_attempted_;
        }

        auto phase() const -> Phase
        {
            return phase_;
        }

        auto return_pending() const -> bool
        {
            return return_pending_;
        }

        auto notification_pending() const -> bool
        {
            return notification_pending_;
        }

        auto notification_attempted() const -> bool
        {
            return notification_attempted_;
        }

        auto was_notification_shown() const -> bool
        {
            return notification_shown_;
        }

        auto phase_name() const -> std::string_view
        {
            switch (phase_)
            {
            case Phase::healthy:
                return "healthy";
            case Phase::failed:
                return "failed";
            case Phase::exiting:
                return "exiting";
            case Phase::returned:
                return "returned";
            }
            return "failed";
        }

      private:
        Phase phase_{Phase::healthy};
        bool return_pending_{};
        bool notification_pending_{};
        bool notification_attempted_{};
        bool notification_shown_{};
        Clock::time_point return_deadline_{};
        Clock::time_point notification_deadline_{};
    };
} // namespace kh3ap
