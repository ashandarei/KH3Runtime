#include <optional>
#include "kh3ap/features/player/combat/magic_command_ids.h"
#include <array>
#include <cassert>
#include <set>

int main()
{
    using namespace kh3ap::magic_command;
    // Retail command IDs: normal tiers and the situation/underwater assets for each family.
    constexpr std::array<std::array<int, 3>, 6> normal{
        {{29, 30, 31}, {33, 34, 35}, {37, 38, 39}, {41, 42, 43}, {45, 46, 47}, {49, 50, 51}}};
    constexpr std::array<std::array<int, 4>, 6> special{{{110, 111, 112, 53},
                                                         {113, 114, 115, 54},
                                                         {116, 117, 118, 55},
                                                         {119, 120, 121, -1},
                                                         {122, 123, 124, 56},
                                                         {125, -1, -1, -1}}};
    // Exhaust every independent choice of locked/first/second/third tier across the six families.
    constexpr int choices_per_family = 4;
    constexpr int configurations = 4096;
    for (int configuration = 0; configuration < configurations; ++configuration)
    {
        int remaining = configuration;
        Commands commands{};
        std::set<int> implementations;
        for (size_t family = 0; family < normal.size(); ++family)
        {
            const int tier = remaining % choices_per_family;
            remaining /= choices_per_family;
            commands[family] = tier ? static_cast<uint8_t>(normal[family][tier - 1]) : 0;
            implementations.insert(commands[family]);
            if (tier)
            {
                for (int command : special[family])
                {
                    if (command >= 0)
                    {
                        implementations.insert(command);
                    }
                }
            }
        }
        for (int command = 0; command <= 255; ++command)
        {
            assert(retains_implementation(static_cast<uint8_t>(command), commands) ==
                   implementations.contains(command));
            bool selected = false;
            for (auto value : commands)
            {
                selected |= value != 0 && value == command;
            }
            assert(is_selected_command(command, commands) == selected);
        }
        assert(!is_selected_command(-1, commands));
        assert(!is_selected_command(256, commands));
    }
    for (int command = -1; command <= 256; ++command)
    {
        std::optional<size_t> expected;
        for (size_t family = 0; family < normal.size(); ++family)
        {
            for (int tier : normal[family])
            {
                if (tier == command)
                {
                    expected = family;
                }
            }
        }
        assert(normal_family(command) == expected);
    }
}
