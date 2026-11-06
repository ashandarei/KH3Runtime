#pragma once

#include <filesystem>
#include <optional>
#include <string_view>

namespace RC::Unreal
{
    class UnrealScriptFunctionCallableContext;
}

namespace kh3ap
{
    class RuntimeState;
    struct HookTargetInfo;
    enum class HookPhase;

    struct RuntimeMapJumpCommandDeps
    {
        RuntimeState& runtime_state;
        std::optional<std::filesystem::path> bridge_dir;
        std::wstring_view request_filename;
        std::wstring_view result_filename;
        std::wstring_view events_filename;
        std::wstring_view protocol_name;
        int protocol_version{};
        std::wstring_view build_stamp;
        std::wstring_view map_jump_function_path;
        std::wstring_view blueprint_library_default_object_path;
        bool debug_logging{};
    };

    auto process_pending_runtime_map_jump_command(const RuntimeMapJumpCommandDeps& deps) -> void;
    auto handle_map_jump_player_start(RC::Unreal::UnrealScriptFunctionCallableContext& context,
                                     HookTargetInfo& hook, HookPhase phase) -> void;
}
