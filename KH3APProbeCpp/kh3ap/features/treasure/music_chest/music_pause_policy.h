#pragma once

#include <chrono>

namespace kh3ap
{
    class RuntimeMusicPausePolicy
    {
    public:
        using Clock = std::chrono::steady_clock;
        static constexpr auto launch_timeout = std::chrono::seconds{30};

        bool active{};
        bool left{};
        Clock::time_point deadline{};

        void begin(Clock::time_point now)
        {
            active = true;
            left = false;
            deadline = now + launch_timeout;
        }

        bool should_release(bool foreground, Clock::time_point now)
        {
            if (!active)
            {
                return false;
            }
            if (!foreground)
            {
                left = true;
            }
            // A rejected/missing launch must not trap the player in a paused game.
            return foreground && (left || now >= deadline);
        }

        void finish()
        {
            active = false;
            left = false;
        }
    };
}
