#pragma once
#include <chrono>
#include <cstdint>
#include <deque>

namespace kh3ap
{
    class Kh02AttackInputBuffer
    {
      public:
        using Clock = std::chrono::steady_clock;

        struct Intent
        {
            uint64_t serial{};
            int index{};
            bool air{};
            bool attempted{};
            Clock::time_point deadline{};
            Clock::time_point attempted_at{};
        };

        auto push(int next_index, bool air, Clock::time_point deadline) -> bool
        {
            if (intents.size() >= 2)
            {
                return false;
            }
            const int index = intents.empty() ? next_index : intents.back().index + 1;
            if (index < 0 || index >= 64)
            {
                return false;
            }
            intents.push_back({++serial, index, air, false, deadline, {}});
            return true;
        }

        auto front() const -> const Intent*
        {
            return intents.empty() ? nullptr : &intents.front();
        }

        auto size() const -> size_t
        {
            return intents.size();
        }

        void attempted(Clock::time_point now)
        {
            if (!intents.empty())
            {
                intents.front().attempted = true;
                intents.front().attempted_at = now;
            }
        }

        void accepted()
        {
            if (!intents.empty())
            {
                intents.pop_front();
            }
        }

        void handoff()
        {
            if (!intents.empty())
            {
                intents.front().attempted = false;
            }
        }

        void expire(Clock::time_point now)
        {
            if (!intents.empty() && now > intents.front().deadline)
            {
                clear();
            }
        }

        void clear()
        {
            intents.clear();
        }

      private:
        std::deque<Intent> intents;
        uint64_t serial{};
    };
} // namespace kh3ap
