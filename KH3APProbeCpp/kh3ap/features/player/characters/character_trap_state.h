#pragma once

#include <chrono>

namespace kh3ap::character_swap
{
    inline constexpr auto character_trap_prepare_timeout = std::chrono::seconds(10);

    // Timing and manual-input ownership are values; UObject/world lifetime is
    // checked by the existing character-swap transaction on the game thread.
    struct CharacterTrapState
    {
        using Clock = std::chrono::steady_clock;
        enum class Phase
        {
            Idle,
            Preparing,
            Starting,
            Active,
            Returning
        };
        Phase phase{Phase::Idle};
        Clock::time_point expires{};
        int return_player{-1};

        bool locked() const
        {
            return phase != Phase::Idle;
        }
        bool due(Clock::time_point now) const
        {
            return phase == Phase::Active && now >= expires;
        }
        void prepare(int player)
        {
            return_player = player;
            phase = Phase::Preparing;
        }
        void activated(Clock::time_point now)
        {
            phase = Phase::Active;
            expires = now + character_trap_prepare_timeout;
        }
        void clear()
        {
            *this = {};
        }
    };
} // namespace kh3ap::character_swap
