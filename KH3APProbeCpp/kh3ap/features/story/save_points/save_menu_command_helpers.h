#pragma once

#include <filesystem>
#include <optional>
#include <string_view>

namespace kh3ap
{
    class RuntimeState;

    struct RuntimeSaveMenuCommandDeps
    {
        RuntimeState& runtime_state;
        std::optional<std::filesystem::path> bridge_dir;
        std::wstring_view request_filename;
        std::wstring_view result_filename;
        std::wstring_view events_filename;
        std::wstring_view protocol_name;
        int protocol_version{};
        std::wstring_view build_stamp;
        std::wstring_view actor_location_function_path;
        std::wstring_view reactor_command_delegate_signature_path;
        std::wstring_view set_action_command_function_path;
        std::wstring_view start_action_command_function_path;
        std::wstring_view simulate_keypress_function_path;
        std::wstring_view set_game_flag_function_path;
        std::wstring_view set_game_flag_boolean_function_path;
        std::wstring_view set_flag_label_function_path;
        std::wstring_view get_game_flag_function_path;
        std::wstring_view get_game_flag_boolean_function_path;
        std::wstring_view blueprint_library_default_object_path;
        std::wstring_view world_map_progress_flag;
        bool debug_logging{};
    };

    auto process_pending_runtime_save_menu_command(const RuntimeSaveMenuCommandDeps& deps) -> void;
}
