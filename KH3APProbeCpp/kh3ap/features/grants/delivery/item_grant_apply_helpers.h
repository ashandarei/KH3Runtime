#pragma once

#include "kh3ap/features/player/state/sora_current_state_cache_view.h"

#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace RC::Unreal
{
    class UObject;
}

namespace kh3ap
{
    class RuntimeState;
    class RuntimeControllerAbilityService;
    struct RuntimeItemApplyPayload;
    struct RuntimeItemGrantRequest;

    struct RuntimeControllerUnlockGrantDeps
    {
        RuntimeControllerAbilityService& controller_abilities;
    };

    struct RuntimeItemGrantApplyDeps
    {
        RuntimeState& runtime_state;
        std::wstring_view bp_get_item_path;
        std::wstring_view bp_add_munny_path;
        std::wstring_view set_game_flag_boolean_path;
        std::wstring_view bp_tres_start_game_over_path;
        std::wstring_view tres_game_blueprint_library_default_object_path;
        std::wstring_view tresui_open_information_path;
        std::wstring_view progressive_keyblade_item_manager_path;
        std::wstring_view runtime_context_summary;
        bool debug_logging;
        RuntimeControllerUnlockGrantDeps controller_unlock_deps;
        std::function<bool(
                std::wstring_view,
                RC::Unreal::UObject*,
                std::wstring*)> party_member_unlock{};
        RuntimeSoraCurrentStateCacheView sora_current_state_cache;
        std::function<void()> progressive_keyblade_level_applied{};
        bool auto_equip_movement_abilities{};
    };

    auto make_runtime_item_grant_apply_deps(
            RuntimeState& runtime_state,
            std::wstring_view bp_get_item_path,
            std::wstring_view bp_add_munny_path,
            std::wstring_view set_game_flag_boolean_path,
            std::wstring_view bp_tres_start_game_over_path,
            std::wstring_view tres_game_blueprint_library_default_object_path,
            std::wstring_view tresui_open_information_path,
            std::wstring_view progressive_keyblade_item_manager_path,
            std::wstring_view runtime_context_summary,
            bool debug_logging,
            RuntimeControllerUnlockGrantDeps controller_unlock_deps,
            RuntimeSoraCurrentStateCacheView sora_current_state_cache = {}) -> RuntimeItemGrantApplyDeps;

    auto try_apply_runtime_item_grant_request(
            const RuntimeItemGrantRequest& request,
            const std::vector<RC::Unreal::UObject*>& world_context_candidates,
            const RuntimeItemGrantApplyDeps& deps,
            RuntimeItemApplyPayload* payload,
            std::wstring* failure_details,
            RC::Unreal::UObject** applied_world_context_object,
            bool* unsafe_abort_requested = nullptr) -> bool;
}
