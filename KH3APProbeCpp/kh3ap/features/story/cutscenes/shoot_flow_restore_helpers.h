#pragma once

#include "kh3ap/features/story/cutscenes/shoot_flow_restore_state.h"

#include <chrono>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

namespace RC::Unreal
{
    class UObject;
}

namespace kh3ap
{
    class RuntimeState;

    struct RuntimeShootFlowRestoreConfig
    {
        RuntimeState& runtime_state;
        std::function<std::optional<std::filesystem::path>()> bridge_dir_resolver;
        std::function<std::wstring()> runtime_context_resolver;
        std::wstring_view build_stamp;
        std::wstring_view ability_function_name;
        std::wstring_view event_filename;
        std::wstring_view events_filename;
        std::chrono::milliseconds settle_delay;
    };

    class RuntimeShootFlowRestoreService
    {
    public:
        explicit RuntimeShootFlowRestoreService(RuntimeShootFlowRestoreConfig config);

        auto observe_pause_ui(bool active) -> void;
        auto tick(std::wstring_view current_sora_state_class) -> void;

    private:
        auto update_context() -> bool;
        auto append_event(
                std::wstring_view status,
                RC::Unreal::UObject* cached_controller,
                std::wstring_view cached_controller_name,
                std::wstring_view details) -> void;

        RuntimeShootFlowRestoreConfig config_;
        RuntimeShootFlowRestoreState state_{};
        RC::Unreal::UObject* pawn_{};
        bool context_active_{false};
    };
}
