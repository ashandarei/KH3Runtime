#pragma once

#include <chrono>

namespace kh3ap
{
    inline constexpr auto keyblade_style_settle_interval = std::chrono::milliseconds(500);
    inline constexpr auto keyblade_style_recovery_interval = std::chrono::seconds(5);

    class RuntimeKeybladeStyleSchedule
    {
        using Clock = std::chrono::steady_clock;
        Clock::time_point recovery_due_{};
        Clock::time_point settle_due_{};

    public:
        [[nodiscard]] auto has_pending_settle() const -> bool
        {
            return settle_due_ != Clock::time_point{};
        }

        auto take(Clock::time_point now, bool dirty) -> bool
        {
            const bool settle = settle_due_ != Clock::time_point{} && now >= settle_due_;
            if (!dirty && !settle && now < recovery_due_)
            {
                return false;
            }
            // The weapon-change event can precede the equipment write. Recheck
            // once at the next normal pass so an early read cannot delay it 5s.
            settle_due_ = dirty ? now + keyblade_style_settle_interval : Clock::time_point{};
            recovery_due_ = now + keyblade_style_recovery_interval;
            return true;
        }
    };
} // namespace kh3ap
