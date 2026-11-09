#pragma once

#include "kh3ap/features/player/player_action_commands.h"
#include <cstdint>

namespace kh3ap::chest_command
{
    // Shipping reactor commands: locked AP chests keep the examine prompt;
    // an eligible chest switches back to the game's normal open command.
    inline constexpr uint8_t open = 3;
    inline constexpr uint8_t examine = 4;

    // Alternate characters must dispatch the selected reactor's action, not
    // turn a locked or Melody chest's Examine prompt into Open.
    constexpr PlayerActionCommand action_for_reactor(uint8_t command)
    {
        return command == open ? PlayerActionCommand::OPEN
             : command == examine ? PlayerActionCommand::CHECK
                                  : PlayerActionCommand::Invalid;
    }
} // namespace kh3ap::chest_command
