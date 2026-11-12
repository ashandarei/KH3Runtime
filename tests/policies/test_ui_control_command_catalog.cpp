#include "kh3ap/features/world/ui_control/command_catalog.h"
#include <algorithm>
#include <array>
#include <cassert>
#include <string_view>

int main()
{
    using namespace kh3ap::ui_control_detail;
    constexpr std::array steam_commands{L"kh02_remote",
                                        L"garden_dark_world_inspect",
                                        L"garden_dark_world_enter",
                                        L"kh02_voice_inspect",
                                        L"kh02_voice_en",
                                        L"kh02_voice_ja",
                                        L"inspect",
                                        L"retry",
                                        L"continue",
                                        L"game_over",
                                        L"select_sora",
                                        L"skip_cutscene",
                                        L"dismiss_key_item",
                                        L"command_inspect",
                                        L"command_step",
                                        L"bootstrap_inspect",
                                        L"bootstrap_remove_ui"};
    constexpr std::array commands_without_pawn{L"inspect",       L"space_dialogue_test",
                                               L"tutorial_test", L"kh02_voice_inspect",
                                               L"kh02_voice_en", L"kh02_voice_ja"};
    assert(!find_command(L""));
    assert(!find_command(L"unsupported_command"));
    for (size_t index = 0; index < command_definitions.size(); ++index)
    {
        const auto& command = command_definitions[index];
        assert(find_command(command.name) == &command);
        assert(command.kind != CommandKind::invalid);
        for (size_t other = index + 1; other < command_definitions.size(); ++other)
        {
            assert(command.name != command_definitions[other].name);
        }
        const bool steam = std::find(steam_commands.begin(), steam_commands.end(), command.name) !=
                           steam_commands.end();
        const bool without_pawn =
            std::find(commands_without_pawn.begin(), commands_without_pawn.end(), command.name) !=
            commands_without_pawn.end();
        assert(command.supports_steam == steam);
        assert(command.requires_pawn == !without_pawn);
        assert(command.requires_message ==
               (command.name == L"dismiss_message" || command.name == L"acknowledge_notice"));
    }
    assert(find_command(L"retry")->kind == find_command(L"continue")->kind);
    assert(find_command(L"retry")->kind == find_command(L"space_retry")->kind);
    assert(find_command(L"skip_cutscene")->kind == find_command(L"resume_cutscene")->kind);
    assert(find_command(L"inspect")->kind != find_command(L"select_sora")->kind);
    assert(find_command(L"kh02_remote")->requires_pawn);
    assert(!find_command(L"space_retry")->supports_steam);
    for (auto name : steam_commands)
    {
        assert(find_command(name));
    }
    for (auto name : commands_without_pawn)
    {
        assert(find_command(name));
    }
}
