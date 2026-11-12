#pragma once
#include <array>
#include <string_view>

namespace kh3ap::ui_control_detail
{
    enum class CommandKind
    {
        invalid,
        event_benchmark,
        ingredient_benchmark,
        kh02_remote,
        voice_language,
        bootstrap,
        viewport,
        frozen_ground,
        garden_travel,
        garden_dialog,
        dark_world_travel,
        computer_space,
        space_dialogue,
        tutorial,
        magic_fixture,
        command_step,
        command_inspect,
        player_selection,
        workshop_materials,
        workshop,
        notice,
        dismiss_message,
        dismiss_key_item,
        retry,
        space_game_over,
        game_over,
        inspect,
        cutscene,
    };
    struct CommandDefinition
    {
        std::wstring_view name;
        CommandKind kind;
        bool supports_steam;
        bool requires_pawn;
        bool requires_message;
    };
    inline constexpr auto command_definitions = std::to_array<CommandDefinition>({
        {L"event_read_benchmark", CommandKind::event_benchmark, false, true, false},
        {L"ingredient_benchmark", CommandKind::ingredient_benchmark, false, true, false},
        {L"kh02_remote", CommandKind::kh02_remote, true, true, false},
        {L"kh02_voice_inspect", CommandKind::voice_language, true, false, false},
        {L"kh02_voice_en", CommandKind::voice_language, true, false, false},
        {L"kh02_voice_ja", CommandKind::voice_language, true, false, false},
        {L"bootstrap_inspect", CommandKind::bootstrap, true, true, false},
        {L"bootstrap_remove_ui", CommandKind::bootstrap, true, true, false},
        {L"viewport_inspect", CommandKind::viewport, false, true, false},
        {L"viewport_set", CommandKind::viewport, false, true, false},
        {L"frozen_ground", CommandKind::frozen_ground, false, true, false},
        {L"garden_frozen_list", CommandKind::garden_travel, false, true, false},
        {L"garden_frozen_travel", CommandKind::garden_travel, false, true, false},
        {L"garden_graveyard_travel", CommandKind::garden_travel, false, true, false},
        {L"battlegate_zero_inspect", CommandKind::garden_dialog, false, true, false},
        {L"battlegate_zero_enter", CommandKind::garden_dialog, false, true, false},
        {L"garden_dark_world_inspect", CommandKind::garden_dialog, true, true, false},
        {L"garden_dark_world_enter", CommandKind::garden_dialog, true, true, false},
        {L"garden_dark_world_travel", CommandKind::dark_world_travel, false, true, false},
        {L"computer_space_test", CommandKind::computer_space, false, true, false},
        {L"space_dialogue_test", CommandKind::space_dialogue, false, false, false},
        {L"tutorial_test", CommandKind::tutorial, false, false, false},
        {L"magic_fixture_inspect", CommandKind::magic_fixture, false, true, false},
        {L"magic_fixture_disable", CommandKind::magic_fixture, false, true, false},
        {L"magic_fixture_restore", CommandKind::magic_fixture, false, true, false},
        {L"command_step", CommandKind::command_step, true, true, false},
        {L"command_inspect", CommandKind::command_inspect, true, true, false},
        {L"select_sora", CommandKind::player_selection, true, true, false},
        {L"workshop_test_materials", CommandKind::workshop_materials, false, true, false},
        {L"workshop_inspect", CommandKind::workshop, false, true, false},
        {L"synthesize_ether", CommandKind::workshop, false, true, false},
        {L"workshop_deposit", CommandKind::workshop, false, true, false},
        {L"acknowledge_notice", CommandKind::notice, false, true, true},
        {L"dismiss_message", CommandKind::dismiss_message, false, true, true},
        {L"dismiss_key_item", CommandKind::dismiss_key_item, false, true, false},
        {L"retry", CommandKind::retry, true, true, false},
        {L"continue", CommandKind::retry, true, true, false},
        {L"space_retry", CommandKind::retry, false, true, false},
        {L"space_game_over", CommandKind::space_game_over, false, true, false},
        {L"game_over", CommandKind::game_over, true, true, false},
        {L"inspect", CommandKind::inspect, true, false, false},
        {L"skip_cutscene", CommandKind::cutscene, true, true, false},
        {L"pause_cutscene", CommandKind::cutscene, false, true, false},
        {L"resume_cutscene", CommandKind::cutscene, false, true, false},
    });
    [[nodiscard]] constexpr const CommandDefinition* find_command(std::wstring_view name) noexcept
    {
        for (const auto& definition : command_definitions)
        {
            if (definition.name == name)
            {
                return &definition;
            }
        }
        return nullptr;
    }
} // namespace kh3ap::ui_control_detail
