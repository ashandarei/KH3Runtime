#pragma once

#include "kh3ap/features/player/context/player_context_service.h"
#include <Unreal/FWeakObjectPtr.hpp>
#include <cstdint>

namespace kh3ap
{
    using RC::Unreal::FWeakObjectPtr;
    using RC::Unreal::UFunction;
    using RC::Unreal::UObject;

    // All entrypoints retain the service's existing game-thread ownership.
    // Weak handles and readiness gates remain together across component calls.
    struct RuntimePlayerContextService::Impl
    {
        explicit Impl(RuntimePlayerContextConfig value);

        auto bridge_dir() const -> std::optional<std::filesystem::path>;

        auto runtime_context() const -> std::wstring;

        auto reset_playable_context() -> void;

        auto read_object_property_in_chain(UObject* object, const wchar_t* property_name) const
            -> UObject*;

        auto format_probe_object(UObject* object) const -> std::wstring;

        auto is_player_controller_name(std::wstring_view object_name) const -> bool;

        auto is_player_controller_object(UObject* object) const -> bool;

        auto log_controller_pawn_cache_probe(UObject* controller, std::wstring_view source) const
            -> void;

        auto write_new_game_staging_current_slot(std::wstring_view source,
                                                 const RuntimeContextSnapshot& snapshot) -> void;

        auto maybe_write_new_game_staging_from_pawn_name(std::wstring_view source,
                                                         std::wstring_view pawn_name) -> bool;

        auto try_write_new_game_staging_from_controller_pawn(UObject* controller,
                                                             std::wstring_view source) -> bool;

        auto is_validated_sora_pawn_name(std::wstring_view pawn_name,
                                         std::wstring_view class_name) const -> bool;

        auto cache_pawn_candidate(UObject* pawn, std::wstring_view source) -> bool;

        auto cache_validated_sora_pawn_candidate(UObject* pawn, std::wstring_view pawn_name,
                                                 std::wstring_view source,
                                                 bool allow_during_special_battle) -> bool;

        auto cache_constructed_sora_pawn(UObject* pawn, std::wstring_view source) -> bool;

        auto cache_pawn_from_object(UObject* object, std::wstring_view source) -> bool;

        auto try_cache_final_world_tutorial_pawn_candidate(UObject* pawn, std::wstring_view source,
                                                           std::wstring_view caller) -> bool;

        auto call_get_local_tres_player_pawn_base_once(UObject* world_context_object,
                                                       std::wstring_view source) -> UObject*;

        auto try_capture_final_world_tutorial_pawn_once(UObject* world_context_object,
                                                        std::wstring_view source,
                                                        std::wstring_view caller) -> bool;

        auto cache_controller_pawn_candidate_fast(UObject* controller,
                                                  std::wstring_view controller_name, UObject* pawn,
                                                  std::wstring_view source,
                                                  bool trust_controller_pawn,
                                                  bool allow_during_special_battle) -> bool;

        auto refresh_controller_from_sora_event(UObject* pawn) -> bool;

        auto cache_pawn_from_player_controller(UObject* controller, std::wstring_view source,
                                               std::wstring_view controller_name_hint,
                                               bool trust_cached_controller) -> bool;

        auto read_object_function_param(UFunction* function_object, void* params,
                                        std::wstring_view param_name) const -> UObject*;

        auto cache_pawn_from_hook_context(UObject* context, UObject* frame_object,
                                          std::wstring_view source) -> bool;

        auto publish_playable_context_from_validated_pawn_name(
            UObject* pawn, std::wstring_view pawn_name, std::chrono::steady_clock::time_point now,
            std::wstring_view pawn_source) -> void;

        auto trusted_current_playable_cached_sora_pawn() -> UObject*;

        auto mark_trusted_current_playable_context_map_ready(std::wstring_view source)
            -> std::optional<RuntimeContextSnapshot>;

        auto write_map_ready_marker(std::wstring_view source, UObject* context,
                                    UObject* frame_object, bool promote_trusted_playable_context)
            -> bool;

        auto process_pending_grant_from_runtime_ready_event(std::wstring_view reason,
                                                            bool refresh_playable_context) -> void;

        auto bootstrap_sora_state_from_trusted_pawn(UObject* pawn, std::wstring_view source)
            -> bool;

        auto revalidate_sora_state_from_event(std::wstring_view source, bool log_unchanged) -> bool;

        auto maybe_clear_special_battle_from_map_root_change(std::wstring_view observed_name,
                                                             std::wstring_view source) -> bool;

        auto maybe_clear_special_battle_from_game_over_recovery(std::wstring_view observed_name,
                                                                std::wstring_view source) -> bool;

        auto maybe_clear_special_battle_from_sora_level_owner(UObject* level_owner) -> bool;

        auto is_bistro_starting_player(std::wstring_view new_player_name) const -> bool;

        auto mark_bistro_visited_from_starting_player(std::wstring_view new_player_name) -> void;

        auto mark_bistro_return_waiting_for_map_name(std::wstring_view source) -> void;

        auto release_bistro_return_gate_from_map_name(std::wstring_view source) -> bool;

        auto clear_blocking_ui_from_starting_player(std::wstring_view new_player_name) -> void;

        auto clear_blocking_ui_for_playable_context(std::wstring_view reason,
                                                    std::wstring_view event_name) -> void;

        auto activate_blocking_ui_from_sora_game_over_state(std::wstring_view state_class,
                                                            const std::optional<uint8_t>& state_id)
            -> void;

        RuntimePlayerContextConfig config;
        RC::Unreal::FWeakObjectPtr local_player{};
        std::wstring map_ready_context_key{};
        FWeakObjectPtr map_ready_pawn{}, map_ready_world{};
        bool special_battle_game_over_recovery_pending{false};
        bool final_world_capture_armed{false};
        bool final_world_capture_closed{false};
        bool final_world_capture_flag_failure_logged{false};
    };

    namespace player_context_detail
    {
        auto lower_ascii_copy(std::wstring_view value) -> std::wstring;

        auto contains_new_game_prep_runtime_path(std::wstring_view value) -> bool;

        auto try_read_process_u8(uintptr_t address) -> std::optional<uint8_t>;

        auto try_read_process_u64(uintptr_t address) -> std::optional<uint64_t>;

        auto read_u8_object_property(UObject* object, const wchar_t* property_name)
            -> std::optional<uint8_t>;

        auto format_object_class_name(UObject* object) -> std::wstring;

    } // namespace player_context_detail
} // namespace kh3ap
