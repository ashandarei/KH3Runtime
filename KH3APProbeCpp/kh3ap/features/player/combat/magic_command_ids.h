#pragma once
#include "kh3ap/features/player/player_action_commands.h"
#include <array>
#include <cstdint>
#include <cstddef>
#include <limits>
#include <optional>

namespace kh3ap::magic_command
{
    enum class Family : int
    {
        Fire,
        Blizzard,
        Thunder,
        Aero,
        Water,
        Cure,
        Count,
    };

    // ETresCommandKind normal spells reserve four IDs per family; three are
    // the ordinary spell tiers. Situation and underwater commands use their
    // own ranges and still require the matching family's implementation assets.
    inline constexpr int family_count = static_cast<int>(Family::Count);
    inline constexpr int first_normal = player_action_command_value(PlayerActionCommand::FIRE);
    inline constexpr int last_normal = player_action_command_value(PlayerActionCommand::CURAGA);
    inline constexpr int normal_family_stride = 4;
    inline constexpr int last_normal_tier_offset = 2;
    inline constexpr int first_situation = player_action_command_value(PlayerActionCommand::S_FIRA);
    inline constexpr int last_situation = player_action_command_value(PlayerActionCommand::S_AEROGAN);
    inline constexpr int situation_family_stride = 3;
    inline constexpr int situation_cure = player_action_command_value(PlayerActionCommand::S_CURAGAN);
    inline constexpr int cure_family_index = static_cast<int>(Family::Cure);
    inline constexpr int first_underwater = player_action_command_value(PlayerActionCommand::WATERFIRE);
    inline constexpr int last_underwater = player_action_command_value(PlayerActionCommand::WATERAERO);
    using Commands = std::array<uint8_t, family_count>;

    [[nodiscard]] constexpr std::optional<size_t> normal_family(int command) noexcept
    {
        if (command < first_normal || command > last_normal ||
            (command - first_normal) % normal_family_stride > last_normal_tier_offset)
        {
            return std::nullopt;
        }
        return static_cast<size_t>((command - first_normal) / normal_family_stride);
    }

    [[nodiscard]] constexpr bool is_selected_command(int command, const Commands& commands) noexcept
    {
        if (command <= 0 || command > std::numeric_limits<uint8_t>::max())
        {
            return false;
        }
        for (auto selected : commands)
        {
            if (selected == command)
            {
                return true;
            }
        }
        return false;
    }

    // Special casts need their implementation assets even though the ordinary
    // menu exposes only the currently owned normal spell in each family.
    [[nodiscard]] constexpr bool retains_implementation(uint8_t command,
                                                        const Commands& commands) noexcept
    {
        for (auto selected : commands)
        {
            if (selected == command)
            {
                return true;
            }
        }
        if (command >= first_situation && command <= last_situation)
        {
            return commands[(command - first_situation) / situation_family_stride] != 0;
        }
        if (command == situation_cure)
        {
            return commands[cure_family_index] != 0;
        }
        if (command >= first_underwater && command <= last_underwater)
        {
            constexpr std::array families{Family::Fire, Family::Blizzard, Family::Thunder,
                                          Family::Water};
            return commands[static_cast<size_t>(families[command - first_underwater])] != 0;
        }
        return false;
    }
} // namespace kh3ap::magic_command
