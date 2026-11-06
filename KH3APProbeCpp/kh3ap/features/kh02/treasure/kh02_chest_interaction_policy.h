#pragma once
#include "kh3ap/features/player/player_state_ids.h"
#include <cstdint>

namespace kh3ap::kh02_chest_interaction
{
    inline constexpr uint8_t idle_state = player_state_ids::idle;
    inline constexpr uint8_t run_state = player_state_ids::run;
    inline constexpr int32_t open_chest_command = 78;

    inline constexpr auto accepts_player_state(uint8_t state) -> bool
    {
        // Ordinary ground movement can yield to the native chest state.
        // Attacks, damage, airborne movement and cinematics must keep ownership.
        return state == idle_state || state == run_state;
    }
} // namespace kh3ap::kh02_chest_interaction
