#include "kh3ap/features/player/characters/character_reaction_policy.h"
#include <cassert>
#include <array>
#include <utility>

int main()
{
    using namespace kh3ap;
    // Only the four offered ship specials use the ship-state route. Boarding,
    // enemy-ship reactors and adjacent commands retain their own dispatch.
    for (int command = -1; command <= 255; ++command)
    {
        assert(character_reactions::special_ship_command(command) ==
               (command == 95 || command == 96 || command == 97 || command == 98));
    }
    using character_reactions::matches_reactor;
    // Prevent confusing UI reactor IDs with the unrelated player-action enum.
    assert(matches_reactor(player_action_command_value(PlayerActionCommand::TALK), 2));
    assert(matches_reactor(player_action_command_value(PlayerActionCommand::OPEN), 3));
    assert(matches_reactor(player_action_command_value(PlayerActionCommand::RESCUE), 6));
    assert(matches_reactor(player_action_command_value(PlayerActionCommand::VEHICLE), 7));
    assert(matches_reactor(player_action_command_value(PlayerActionCommand::CHECK), 4));
    assert(matches_reactor(player_action_command_value(PlayerActionCommand::TRICK), 15));
    // Shipping enum pairs from the reactor catalog, independent of command IDs.
    constexpr std::array pairs{
        std::pair{77, 2}, std::pair{78, 3}, std::pair{79, 4}, std::pair{81, 6},
        std::pair{82, 7}, std::pair{84, 9}, std::pair{88, 13}, std::pair{94, 14},
        std::pair{91, 15}, std::pair{92, 16}, std::pair{93, 17}, std::pair{100, 18},
        std::pair{102, 19}, std::pair{99, 26}, std::pair{247, 30}};
    for (int command = -1; command <= 255; ++command)
    {
        for (int reactor = 0; reactor <= 255; ++reactor)
        {
            bool expected = false;
            for (const auto& pair : pairs)
            {
                expected |= pair.first == command && pair.second == reactor;
            }
            assert(matches_reactor(command, static_cast<uint8_t>(reactor)) == expected);
        }
    }
    // Cover and combat commands must never be treated as arbitrary reactors.
    assert(!matches_reactor(player_action_command_value(PlayerActionCommand::COVERSLIDE), 15));
    assert(!matches_reactor(player_action_command_value(PlayerActionCommand::FINISH), 4));
    using character_reactions::shared_situation;
    for (auto player : {PlayerId::Aqua, PlayerId::Riku, PlayerId::Kairi, PlayerId::Roxas})
    {
        assert(shared_situation(186, player));
        assert(shared_situation(192, player));
        assert(shared_situation(208, player));
        assert(!shared_situation(127, player)); // No Sora formchange override.
        assert(!shared_situation(229, player)); // Dispel has its own owner gate.
        assert(!shared_situation(238, player)); // Aqua's native Blizzaga Board.
        assert(!shared_situation(255, player));
    }
    assert(!shared_situation(196, PlayerId::Aqua));
    assert(shared_situation(196, PlayerId::Roxas));
    assert(!shared_situation(213, PlayerId::Riku));
    assert(!shared_situation(241, PlayerId::Kairi));
    assert(!shared_situation(242, PlayerId::Kairi));
    for (int command = -1; command <= 255; ++command)
    {
        assert(!shared_situation(command, PlayerId::Sora));
        assert(!shared_situation(command, PlayerId::KingMickey));
    }
}
