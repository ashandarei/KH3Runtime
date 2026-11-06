#pragma once

#include <cstdint>

namespace kh3ap::chest_command
{
    // Shipping reactor commands: locked AP chests keep the examine prompt;
    // an eligible chest switches back to the game's normal open command.
    inline constexpr uint8_t open = 3;
    inline constexpr uint8_t examine = 4;
} // namespace kh3ap::chest_command
