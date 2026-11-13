#pragma once
#include "kh3ap/features/player/player_action_commands.h"
#include <cstdint>

namespace kh3ap::character_reactions
{
    // ETresReactorCommandID is a different enum from ETresCommandKind.
    constexpr bool matches_reactor(int command, uint8_t reactor)
    {
        switch (static_cast<PlayerActionCommand>(command))
        {
        case PlayerActionCommand::TALK: return reactor == 2;
        case PlayerActionCommand::CHECK: return reactor == 4;
        case PlayerActionCommand::RESCUE: return reactor == 6;
        case PlayerActionCommand::VEHICLE: return reactor == 7;
        case PlayerActionCommand::TRICK: return reactor == 15;
        default: return false;
        }
    }
}
