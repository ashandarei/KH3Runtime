#include "kh3ap/features/player/characters/character_reaction_policy.h"
#include <cassert>

int main()
{
    using namespace kh3ap;
    using character_reactions::matches_reactor;
    // Prevent confusing UI reactor IDs with the unrelated player-action enum.
    assert(matches_reactor(player_action_command_value(PlayerActionCommand::TALK), 2));
    assert(matches_reactor(player_action_command_value(PlayerActionCommand::RESCUE), 6));
    assert(matches_reactor(player_action_command_value(PlayerActionCommand::VEHICLE), 7));
    assert(matches_reactor(player_action_command_value(PlayerActionCommand::CHECK), 4));
    assert(matches_reactor(player_action_command_value(PlayerActionCommand::TRICK), 15));
    for (int command = -1; command <= 255; ++command)
    {
        for (int reactor = 0; reactor <= 255; ++reactor)
        {
            if (matches_reactor(command, static_cast<uint8_t>(reactor)))
            {
                assert((command == 77 && reactor == 2) || (command == 79 && reactor == 4) ||
                       (command == 81 && reactor == 6) || (command == 82 && reactor == 7) ||
                       (command == 91 && reactor == 15));
            }
        }
    }
    // Cover and combat commands must never be treated as arbitrary reactors.
    assert(!matches_reactor(player_action_command_value(PlayerActionCommand::COVERSLIDE), 15));
    assert(!matches_reactor(player_action_command_value(PlayerActionCommand::FINISH), 4));
}
