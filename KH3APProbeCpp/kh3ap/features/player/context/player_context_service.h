#pragma once

#include "kh3ap/core/types.h"

#include <chrono>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace RC::Unreal
{
    class UFunction;
    class UObject;
}

namespace kh3ap
{
    class RuntimeBattleLevelSyncService;
    struct RuntimeSoraLevelMonitorState;
    class RuntimeSoraStateHookService;
    class RuntimeState;
    class RuntimeTutorialSkipService;

    struct RuntimePlayerContextConfig
    {
        RuntimeState& runtime_state;
        RuntimeSoraLevelMonitorState& sora_monitor;
        RuntimeBattleLevelSyncService& battle_level_sync;
        RuntimeTutorialSkipService& tutorial_skips;
        RuntimeSoraStateHookService& sora_state_hooks;
        std::function<std::optional<std::filesystem::path>()> bridge_dir_resolver{};
        std::function<std::wstring()> runtime_context_resolver{};
        std::function<void(std::wstring_view, std::wstring_view)> enter_ignored_map{};
        std::function<void(std::wstring_view, std::wstring_view)> leave_ignored_map{};
        std::function<void(std::wstring_view)> disarm_controller_reacquire{};
        std::function<void(
                std::chrono::steady_clock::time_point,
                std::wstring_view)> publish_cached_playable_context{};
        std::function<void(std::wstring_view)> process_pending_grant_now{};
        std::function<void()> on_pawn_changed{};
        std::function<void()> on_map_ready{};
    };

    class RuntimePlayerContextService
    {
    public:
        explicit RuntimePlayerContextService(RuntimePlayerContextConfig config);
        ~RuntimePlayerContextService();

        RuntimePlayerContextService(const RuntimePlayerContextService&) = delete;
        auto operator=(const RuntimePlayerContextService&)
                -> RuntimePlayerContextService& = delete;
        RuntimePlayerContextService(RuntimePlayerContextService&&) noexcept;
        auto operator=(RuntimePlayerContextService&&) noexcept
                -> RuntimePlayerContextService&;

        auto reset_playable_context() -> void;
        auto map_ready_context_key() const -> std::wstring_view;
        auto final_world_capture_armed() const -> bool;
        auto arm_final_world_capture() -> void;
        auto clear_special_battle_recovery_pending() -> void;

        auto maybe_cache_blueprint_library_default_object(
                RC::Unreal::UObject* context,
                bool allow_cached_pointer_skip) -> void;
        auto is_player_controller_name(std::wstring_view object_name) const -> bool;
        auto is_player_controller_object(RC::Unreal::UObject* object) const -> bool;
        auto try_write_new_game_staging_from_controller_pawn(
                RC::Unreal::UObject* controller,
                std::wstring_view source) -> bool;
        auto cache_pawn_candidate(
                RC::Unreal::UObject* pawn,
                std::wstring_view source) -> bool;
        auto is_validated_sora_pawn_name(
                std::wstring_view pawn_name,
                std::wstring_view class_name) const -> bool;
        auto cache_validated_sora_pawn_candidate(
                RC::Unreal::UObject* pawn,
                std::wstring_view pawn_name,
                std::wstring_view source,
                bool allow_during_special_battle = false) -> bool;
        auto cache_constructed_sora_pawn(
                RC::Unreal::UObject* pawn,
                std::wstring_view source) -> bool;
        auto refresh_controller_from_sora_event(RC::Unreal::UObject* pawn) -> bool;
        auto try_capture_final_world_tutorial_pawn_once(
                RC::Unreal::UObject* world_context_object,
                std::wstring_view source,
                std::wstring_view caller) -> bool;
        auto cache_pawn_from_object(
                RC::Unreal::UObject* object,
                std::wstring_view source) -> bool;
        auto cache_pawn_from_player_controller(
                RC::Unreal::UObject* controller,
                std::wstring_view source,
                std::wstring_view controller_name_hint = {},
                bool trust_cached_controller = false) -> bool;
        auto read_object_function_param(
                RC::Unreal::UFunction* function_object,
                void* params,
                std::wstring_view param_name) const -> RC::Unreal::UObject*;
        auto cache_pawn_from_hook_context(
                RC::Unreal::UObject* context,
                RC::Unreal::UObject* frame_object,
                std::wstring_view source) -> bool;
        auto write_map_ready_marker(
                std::wstring_view source,
                RC::Unreal::UObject* context,
                RC::Unreal::UObject* frame_object,
                bool promote_trusted_playable_context = false) -> bool;
        auto bootstrap_sora_state_from_trusted_pawn(
                RC::Unreal::UObject* pawn,
                std::wstring_view source) -> bool;
        auto revalidate_sora_state_from_event(
                std::wstring_view source,
                bool log_unchanged = true) -> bool;
        auto process_pending_grant_from_runtime_ready_event(
                std::wstring_view reason,
                bool refresh_playable_context = true) -> void;
        auto maybe_clear_special_battle_from_map_root_change(
                std::wstring_view observed_name,
                std::wstring_view source) -> bool;
        auto maybe_clear_special_battle_from_game_over_recovery(
                std::wstring_view observed_name,
                std::wstring_view source) -> bool;
        auto maybe_clear_special_battle_from_sora_level_owner(
                RC::Unreal::UObject* level_owner) -> bool;
        auto clear_blocking_ui_from_starting_player(
                std::wstring_view new_player_name) -> void;
        auto clear_blocking_ui_for_playable_context(
                std::wstring_view reason,
                std::wstring_view event_name) -> void;
        auto activate_blocking_ui_from_sora_game_over_state(
                std::wstring_view state_class,
                const std::optional<uint8_t>& state_id) -> void;
        auto is_bistro_starting_player(std::wstring_view new_player_name) const -> bool;
        auto mark_bistro_visited_from_starting_player(
                std::wstring_view new_player_name) -> void;
        auto mark_bistro_return_waiting_for_map_name(std::wstring_view source) -> void;
        auto release_bistro_return_gate_from_map_name(std::wstring_view source) -> bool;
        auto trusted_current_playable_cached_sora_pawn() const -> RC::Unreal::UObject*;

    private:
        struct Impl;
        std::unique_ptr<Impl> impl_;
    };
}
