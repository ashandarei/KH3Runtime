#pragma once
#include "kh3ap/features/player/player_action_commands.h"
#include "kh3ap/features/player/player_ids.h"
#include <cstdint>

namespace kh3ap::character_reactions
{
    constexpr bool special_ship_command(int command)
    {
        switch (static_cast<PlayerActionCommand>(command))
        {
        case PlayerActionCommand::SPECIALSHIP_RAGINGCANNON:
        case PlayerActionCommand::SPECIALSHIP_WINDCHASER:
        case PlayerActionCommand::SPECIALSHIP_TIDALWAVE:
        case PlayerActionCommand::SPECIALSHIP_DASH:
            return true;
        default:
            return false;
        }
    }

    // Only Sora's audited shared situation branches. Preserve each alternate's
    // authored team dispatcher where it has a character-specific implementation.
    constexpr bool shared_situation(int command, PlayerId player)
    {
        if (player != PlayerId::Aqua && player != PlayerId::Riku &&
            player != PlayerId::Kairi && player != PlayerId::Roxas) { return false; }
        const auto action = static_cast<PlayerActionCommand>(command);
        if ((player == PlayerId::Aqua && (action == PlayerActionCommand::FR_MICKEY ||
                 action == PlayerActionCommand::FR_MICKEYCHAIN)) ||
            (player == PlayerId::Kairi && (action == PlayerActionCommand::FR_LINKOFHEARTS ||
                 action == PlayerActionCommand::FR_PROMISEWINGS)) ||
            (player == PlayerId::Riku && action == PlayerActionCommand::FR_SORA)) { return false; }
        return (command >= player_action_command_value(PlayerActionCommand::FR_DONALD) &&
                command <= player_action_command_value(PlayerActionCommand::FR_GOOFY2)) ||
               (command >= player_action_command_value(PlayerActionCommand::AF_JETCOASTER_BM) &&
                command <= player_action_command_value(PlayerActionCommand::FR_FINISH)) ||
               action == PlayerActionCommand::FR_POPPINGHOLY ||
               action == PlayerActionCommand::FR_ULTIMATEEND ||
               action == PlayerActionCommand::FR_LINKOFHEARTS ||
               action == PlayerActionCommand::FR_PROMISEWINGS;
    }

    // ETresReactorCommandID is a different enum from ETresCommandKind.
    constexpr bool matches_reactor(int command, uint8_t reactor)
    {
        switch (static_cast<PlayerActionCommand>(command))
        {
        case PlayerActionCommand::TALK: return reactor == 2;
        case PlayerActionCommand::OPEN: return reactor == 3;
        case PlayerActionCommand::CHECK: return reactor == 4;
        case PlayerActionCommand::RESCUE: return reactor == 6;
        case PlayerActionCommand::VEHICLE: return reactor == 7;
        case PlayerActionCommand::PLANE: return reactor == 9;
        case PlayerActionCommand::PLANEBOSS: return reactor == 13;
        case PlayerActionCommand::ENEMYSHIP: return reactor == 14;
        case PlayerActionCommand::TRICK: return reactor == 15;
        case PlayerActionCommand::WATERPLAY: return reactor == 16;
        case PlayerActionCommand::EXTENDHAND: return reactor == 17;
        case PlayerActionCommand::PUDDING: return reactor == 18;
        case PlayerActionCommand::POSTCARD: return reactor == 19;
        case PlayerActionCommand::SWING: return reactor == 26;
        case PlayerActionCommand::CARDDRAW: return reactor == 30;
        default: return false;
        }
    }
}
