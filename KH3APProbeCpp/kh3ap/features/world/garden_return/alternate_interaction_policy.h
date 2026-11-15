#pragma once
#include "kh3ap/features/player/player_state_ids.h"
#include <cstdint>

namespace kh3ap::alternate_interaction
{
    constexpr bool accepts_player_state(uint8_t state, bool swimming_target)
    {
        // Both stationary and moving underwater exploration use Swim. Keep
        // combat, damage and other state owners out of the fallback. The native
        // command dispatcher still decides whether the selected action can run.
        return state == player_state_ids::idle || state == player_state_ids::run ||
               (swimming_target && state == player_state_ids::swim);
    }
}
