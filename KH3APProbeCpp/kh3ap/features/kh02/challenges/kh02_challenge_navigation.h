#pragma once
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace kh3ap
{
    // One logical controller direction, even when both a stick and D-pad report
    // it. The overlay polls its monotonic clock each frame. Engine Repeat events
    // cannot be the clock: KH3's UI repeat stream can pause during a held press.
    class Kh02ChallengeControllerNavigation
    {
      public:
        static constexpr int64_t hold_delay_ms = 350, repeat_interval_ms = 140, bounce_ms = 70;

        void reset()
        {
            held.clear();
            direction = 0;
            last_step.reset();
            next_repeat = 0;
            last_direction = 0;
        }

        auto accept(int controller, std::wstring_view key, int delta, int event, int64_t now) -> bool
        {
            const auto id = std::make_pair(controller, std::wstring(key));
            if (event == 1)
            {
                held.erase(id);
                bool remaining = false;
                for (const auto& [_, d] : held)
                {
                    remaining |= d == direction;
                }
                if (!remaining)
                {
                    direction = 0;
                }
                return false;
            }
            if (event == 0)
            {
                if (held.contains(id))
                {
                    return false;
                }
                held.emplace(id, delta);
                if (direction == delta)
                {
                    return false; // Alias of the same held direction.
                }
                direction = delta;
                // Brief neutral jitter must not restart the initial hold delay.
                if (last_step && last_direction == delta && now - *last_step < bounce_ms)
                {
                    return false;
                }
                next_repeat = now + hold_delay_ms;
            }
            else
            {
                return false;
            }
            last_step = now;
            last_direction = delta;
            return true;
        }

        auto poll(int64_t now, int64_t interval_ms = repeat_interval_ms) -> int
        {
            if ((direction != 1 && direction != -1) || now < next_repeat)
            {
                return 0;
            }
            // Never catch up missed repeats in a burst after a slow frame.
            next_repeat = now + interval_ms;
            last_step = now;
            last_direction = direction;
            return direction;
        }

      private:
        std::map<std::pair<int, std::wstring>, int> held;
        int direction{}, last_direction{};
        int64_t next_repeat{};
        std::optional<int64_t> last_step;
    };
} // namespace kh3ap
