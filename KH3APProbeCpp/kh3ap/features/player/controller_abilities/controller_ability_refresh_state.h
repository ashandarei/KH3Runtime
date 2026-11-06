#pragma once

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

namespace kh3ap
{
    [[nodiscard]] inline auto is_runtime_mount_olympus_ability_context(
            std::wstring_view pawn_name) -> bool
    {
        return pawn_name.find(L"/Game/Levels/he/he_02/") != std::wstring_view::npos;
    }

    // Retain successful effects across menu/native notifications. Failed setters
    // remain different and can retry without reapplying the successful ones.
    class RuntimeControllerAbilityAppliedState
    {
    public:
        static constexpr std::size_t ability_count = 8;

        auto reset() -> void { states_.fill(std::nullopt); }
        auto reset_for_map_ready(std::wstring_view pawn_name) -> bool
        {
            if (!is_runtime_mount_olympus_ability_context(pawn_name))
            {
                return false;
            }
            reset();
            return true;
        }
        [[nodiscard]] auto needs_update(size_t index, bool desired) const -> bool
        {
            return !states_[index].has_value() || *states_[index] != desired;
        }
        auto record(size_t index, bool enabled) -> void { states_[index] = enabled; }
        auto invalidate(size_t index) -> void { states_[index].reset(); }

    private:
        std::array<std::optional<bool>, ability_count> states_{};
    };

    // Game-thread work queue: events coalesce, readiness failures get a bounded
    // retry window, and a successful refresh leaves no recurring work armed.
    class RuntimeControllerAbilityRefreshState
    {
    public:
        using Clock = std::chrono::steady_clock;
        static constexpr unsigned maximum_attempts = 30;
        static constexpr auto retry_delay = std::chrono::milliseconds{250};

        auto begin(Clock::time_point now, uint32_t events) -> bool
        {
            if (events != 0)
            {
                events_ |= events;
                attempts_ = 0;
                pending_ = true;
                next_attempt_ = now;
            }
            if (!pending_ || now < next_attempt_)
            {
                return false;
            }
            ++attempts_;
            next_attempt_ = now + retry_delay;
            return true;
        }

        auto finish(bool success) -> void
        {
            if (success || attempts_ >= maximum_attempts)
            {
                pending_ = false;
                events_ = 0;
            }
        }

        [[nodiscard]] auto pending() const -> bool { return pending_; }
        [[nodiscard]] auto attempts() const -> unsigned { return attempts_; }
        [[nodiscard]] auto events() const -> uint32_t { return events_; }

    private:
        bool pending_{};
        unsigned attempts_{};
        uint32_t events_{};
        Clock::time_point next_attempt_{};
    };
}
