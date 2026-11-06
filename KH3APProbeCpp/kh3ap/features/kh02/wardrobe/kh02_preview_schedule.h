#pragma once
#include <chrono>

namespace kh3ap
{
    // Request timeout and capture cadence have different reset boundaries.
    // Rebuilding render resources clears capture cadence but must not extend a pending load.
    class Kh02PreviewSchedule
    {
      public:
        using Clock = std::chrono::steady_clock;
        using TimePoint = Clock::time_point;
        static constexpr auto material_load_timeout = std::chrono::seconds(10);
        static constexpr auto capture_interval = std::chrono::milliseconds(66);

        void material_requested(TimePoint now)
        {
            material_requested_at = now;
        }

        auto material_timed_out(TimePoint now) const -> bool
        {
            return now - material_requested_at > material_load_timeout;
        }

        auto begin_capture(TimePoint now) -> bool
        {
            if (now < next_capture)
            {
                return false;
            }
            next_capture = now + capture_interval;
            return true;
        }

        void reset_capture()
        {
            next_capture = {};
        }

      private:
        TimePoint material_requested_at{};
        TimePoint next_capture{};
    };
} // namespace kh3ap
