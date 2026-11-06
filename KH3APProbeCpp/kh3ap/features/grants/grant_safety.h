#pragma once

#include <chrono>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "kh3ap/features/player/state/sora_current_state_cache_view.h"

namespace RC::Unreal
{
    class UObject;
}

namespace kh3ap
{
    enum class RuntimeSoraStateKind : uint8_t;

    class RuntimeBlockingUiState;
    class RuntimeGrantState;
    class RuntimeGrantPawnState;
    class RuntimeState;

    struct RuntimeGrantSafetyStatus
    {
        bool ready{false};
        std::wstring reason{};
        std::wstring details{};
    };

    auto current_runtime_blocking_ui_status(const RuntimeBlockingUiState& blocking_ui_state) -> RuntimeGrantSafetyStatus;
    auto runtime_space_inventory_safety_status(RuntimeState& runtime_state, bool supported,
            RC::Unreal::UObject** context) -> RuntimeGrantSafetyStatus;

    auto current_runtime_bistro_return_gate_status(
            const RuntimeGrantState& grant_state,
            std::chrono::steady_clock::time_point now) -> RuntimeGrantSafetyStatus;

    auto current_runtime_classic_kingdom_transition_gate_status(
            const RuntimeGrantState& grant_state,
            std::chrono::steady_clock::time_point now) -> RuntimeGrantSafetyStatus;

    auto current_runtime_olympus_coliseum_transition_gate_status(
            const RuntimeGrantState& grant_state,
            std::chrono::steady_clock::time_point now) -> RuntimeGrantSafetyStatus;

    auto current_runtime_special_battle_mode_status(
            const RuntimeGrantState& grant_state,
            std::chrono::steady_clock::time_point now) -> RuntimeGrantSafetyStatus;

    auto current_runtime_unsafe_map_status(RuntimeState& runtime_state, bool allow_excluded_map_game_over = false) -> RuntimeGrantSafetyStatus;

    auto current_runtime_sora_state_cache_status(
            const RuntimeSoraCurrentStateCacheView& sora_current_state_cache) -> RuntimeGrantSafetyStatus;

    auto current_runtime_bad_status_trap_sora_state_cache_status(
            const RuntimeSoraCurrentStateCacheView& sora_current_state_cache) -> RuntimeGrantSafetyStatus;

    auto current_runtime_trap_live_state_status(RC::Unreal::UObject* pawn) -> RuntimeGrantSafetyStatus;
    auto runtime_persistent_reward_safety_status(RuntimeState& runtime_state,
            RC::Unreal::UObject* pawn, std::chrono::steady_clock::time_point now)
            -> RuntimeGrantSafetyStatus;
    auto runtime_alternate_bad_status_safety_status(RuntimeState& runtime_state,
            RC::Unreal::UObject* pawn) -> RuntimeGrantSafetyStatus;
    auto runtime_alternate_game_over_safety_status(RuntimeState& runtime_state,
            RC::Unreal::UObject* pawn) -> RuntimeGrantSafetyStatus;

    auto update_runtime_grant_pawn_presence_status(
            RuntimeGrantPawnState& pawn_state,
            const std::vector<RC::Unreal::UObject*>& candidates,
            std::chrono::steady_clock::time_point now) -> RuntimeGrantSafetyStatus;

    auto runtime_grant_playable_safety_status(
            const RuntimeBlockingUiState& blocking_ui_state,
            RuntimeGrantPawnState& pawn_state,
            const std::vector<RC::Unreal::UObject*>& candidates,
            std::chrono::steady_clock::time_point now) -> RuntimeGrantSafetyStatus;

    auto runtime_cached_sora_pawn_is_available(RuntimeState& runtime_state) -> bool;

    auto runtime_cached_sora_state_matches(
            RuntimeState& runtime_state,
            uint8_t state_id,
            std::wstring_view state_class_name) -> bool;

    auto runtime_cached_sora_state_matches_kind(
            RuntimeState& runtime_state,
            RuntimeSoraStateKind kind) -> bool;

    auto runtime_grant_final_sora_state_safety_status(
            RuntimeState& runtime_state,
            RC::Unreal::UObject* world_context_object,
            const RuntimeSoraCurrentStateCacheView* sora_current_state_cache = nullptr) -> RuntimeGrantSafetyStatus;

    auto collect_world_context_candidates(RuntimeState& runtime_state, RC::Unreal::UObject* preferred_object)
            -> std::vector<RC::Unreal::UObject*>;

    auto collect_sora_ability_pawn_candidates(RuntimeState& runtime_state, RC::Unreal::UObject* preferred_object)
            -> std::vector<RC::Unreal::UObject*>;

    auto collect_cached_runtime_grant_pawn_candidates(RuntimeState& runtime_state, RC::Unreal::UObject* preferred_object)
            -> std::vector<RC::Unreal::UObject*>;

    auto collect_fast_runtime_item_grant_world_context_candidates(RuntimeState& runtime_state, RC::Unreal::UObject* preferred_object)
            -> std::vector<RC::Unreal::UObject*>;

    auto runtime_grant_context_snapshot_safety_status(
            RuntimeState& runtime_state,
            std::chrono::steady_clock::time_point now,
            std::chrono::steady_clock::duration ready_context_debounce,
            bool allow_special_battle_mode = false,
            bool allow_excluded_map_game_over = false) -> RuntimeGrantSafetyStatus;
}
