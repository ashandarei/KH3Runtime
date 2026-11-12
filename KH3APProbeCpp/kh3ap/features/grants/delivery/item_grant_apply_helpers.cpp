#include "ultima_weapon_shard_policy.h"
#include "kh3ap/features/grants/trap_catalog.h"
#include "kh3ap/features/grants/game_over_policy.h"
#include "kh3ap/features/player/characters/character_swap.h"
#include "kh3ap/features/player/characters/character_unlocks.h"
#include "kh3ap/features/grants/delivery/item_grant_apply_helpers.h"
#include "kh3ap/features/world/space/space_service.h"
#include "kh3ap/features/world/portals/portal_service.h"
#include "kh3ap/features/world/space/space_policy.h"

#include "kh3ap/features/grants/messages/ap_message_call_helpers.h"
#include "kh3ap/features/grants/bad_status_helpers.h"
#include "kh3ap/features/player/controller_abilities/controller_ability_service.h"
#include "kh3ap/features/grants/grant_safety.h"
#include "kh3ap/features/grants/grant_tokens.h"
#include "kh3ap/features/grants/delivery/item_call_helpers.h"
#include "kh3ap/features/player/keyblades/keyblade_options_helpers.h"
#include "kh3ap/features/grants/keyblades/progressive_keyblade_level_helpers.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/features/player/state/sora_barrier_helpers.h"
#include "kh3ap/app/state.h"
#include "kh3ap/core/types.h"
#include "kh3ap/features/grants/vbonus/vbonus_grant_call_helpers.h"
#include "kh3ap/bridge/utilities.h"

#include <DynamicOutput/DynamicOutput.hpp>
#include "kh3ap/features/kh02/story/kh02_story.h"
#include "kh3ap/features/kh02/rewards/kh02_reward_policy.h"

#include <UnrealDef.hpp>
#include <Unreal/UObject.hpp>

#include <cstdint>
#include <limits>
#include <format>
#include <string>
#include <utility>
#include <vector>

namespace kh3ap
{
    using namespace RC;
    using namespace RC::Unreal;

    namespace
    {
        constexpr auto grant_safety_log_chars = 160;
        constexpr auto failed_grant_safety_log_chars = 180;
        constexpr auto grant_candidate_failure_log_chars = 96;

        constexpr auto small_munny_pouch_amount = 100;
        constexpr auto large_munny_pouch_amount = 1000;
        constexpr auto item_grant_message_duration_seconds = 6.0;
        auto format_opaque_world_context_address(const UObject* object) -> std::wstring
        {
            return std::format(STR("0x{:X}"), reinterpret_cast<uintptr_t>(object));
        }

        constexpr std::wstring_view s_starlight_runtime_token = STR("WEP_KEYBLADE_SO_018");
        constexpr std::wstring_view s_starlight_story_flag = STR("gameflow_KG_got_Starlight");
        constexpr std::wstring_view s_munny_pouch_runtime_token = STR("KEY_ITEM18");
        constexpr std::wstring_view s_large_munny_pouch_runtime_token = STR("KEY_ITEM19");
        constexpr std::wstring_view s_ultima_weapon_runtime_token = STR("WEP_KEYBLADE_SO_015");
        constexpr std::wstring_view s_ultima_weapon_shard_inventory_runtime_token =
            STR("MAT_ITEM57");
        auto progressive_keyblade_refresh_is_forced(const RuntimeItemGrantRequest& request) -> bool
        {
            return request.reapply_context_key == STR("specific_runtime_grant") ||
                   request.action_kind == STR("runtime_grant_probe") ||
                   request.action_kind == STR("manual_test");
        }

        auto munny_pouch_amount_for_token(std::wstring_view runtime_token) -> int32_t
        {
            if (runtime_token == s_munny_pouch_runtime_token)
            {
                return small_munny_pouch_amount;
            }
            if (runtime_token == s_large_munny_pouch_runtime_token)
            {
                return large_munny_pouch_amount;
            }
            return 0;
        }

        auto runtime_item_grant_is_bad_status_trap(const RuntimeItemGrantRequest& request) -> bool
        {
            return find_runtime_trap_spec(request.runtime_token, request.grant_method) != nullptr;
        }

        auto maybe_apply_starlight_story_flag_after_direct_item_grant(
            bool item_grant_applied, std::wstring_view bp_get_item_token,
            const RuntimeItemGrantRequest& request, const RuntimeItemGrantApplyDeps& deps) -> void
        {
            if (!item_grant_applied || (bp_get_item_token != s_starlight_runtime_token &&
                                        request.runtime_token != s_starlight_runtime_token))
            {
                return;
            }

            std::wstring flag_failure{};
            const bool flag_applied = try_call_set_game_flag_boolean(
                s_starlight_story_flag, true, deps.runtime_state, deps.set_game_flag_boolean_path,
                deps.tres_game_blueprint_library_default_object_path, deps.debug_logging,
                &flag_failure);
            Output::send<LogLevel::Verbose>(
                STR("[KH3APProbeCpp] Direct Starlight grant story flag side effect applied={} "
                    "index={} item={} token={} flag={} failure={}\n"),
                flag_applied ? STR("true") : STR("false"), request.index, request.item_name,
                request.runtime_token, s_starlight_story_flag,
                flag_failure.empty() ? STR("<none>") : flag_failure);
        }

        auto
        maybe_present_controller_unlock_ability(const RuntimeItemGrantRequest& request,
                                                const RuntimeControllerAbilityGrantResult& result,
                                                UObject* sora_pawn,
                                                const RuntimeItemGrantApplyDeps& deps) -> void
        {
            if (RuntimeSpaceService::is_space_active() ||
                persistent_player_reward_context() == sora_pawn ||
                !RuntimeGrantTokens::effective_show_info(request) || result.ability_token.empty())
            {
                return;
            }

            std::wstring failure_reason{};
            if (try_present_native_vbonus_hud_ability_tokens(
                    std::vector<std::wstring>{result.ability_token}, sora_pawn, deps.runtime_state,
                    &failure_reason))
            {
                Output::send<LogLevel::Verbose>(
                    STR("[KH3APProbeCpp] Controller unlock ability HUD shown index={} item={} "
                        "ability_token={} ability={} newly_granted={} equipped={} | {}\n"),
                    request.index, request.item_name, result.ability_token, result.ability_value,
                    result.newly_granted ? STR("true") : STR("false"),
                    result.equipped ? STR("true") : STR("false"),
                    std::wstring{deps.runtime_context_summary});
                return;
            }

            Output::send<LogLevel::Warning>(
                STR("[KH3APProbeCpp] Controller unlock ability HUD failed index={} item={} "
                    "ability_token={} ability={} error={}\n"),
                request.index, request.item_name, result.ability_token, result.ability_value,
                failure_reason.empty() ? std::wstring{STR("unknown")} : failure_reason);
        }

        auto maybe_show_progressive_keyblade_level_information(
            const RuntimeItemGrantRequest& request,
            const ProgressiveKeybladeLevelApplyResult& result,
            const RuntimeItemGrantApplyDeps& deps) -> void
        {
            if (!request.show_info)
            {
                return;
            }

            RuntimeApMessageRequest message_request{};
            message_request.request_id =
                request.request_id.empty()
                    ? std::format(STR("progressive-keyblade-level:{}"), request.index)
                    : std::format(STR("{}:progressive-keyblade-level:{}"), request.request_id,
                                  request.index);
            message_request.message =
                result.applies_to_all_keyblades
                    ? std::format(STR("All Keyblades upgraded to level {}"), result.target_level)
                    : std::format(STR("{} upgraded to level {}"), result.keyblade_name,
                                  result.target_level);
            message_request.item_name = request.item_name;
            message_request.location_name = request.location_name;
            message_request.duration_seconds = item_grant_message_duration_seconds;

            std::wstring failure_reason{};
            if (!try_call_tresui_open_information(
                    message_request, deps.runtime_state, deps.tresui_open_information_path,
                    deps.tres_game_blueprint_library_default_object_path, &failure_reason))
            {
                Output::send<LogLevel::Warning>(
                    STR("[KH3APProbeCpp] Progressive Keyblade Level information failed index={} "
                        "level={} error={}\n"),
                    request.index, result.target_level,
                    failure_reason.empty() ? std::wstring{STR("unknown")} : failure_reason);
            }
        }

        auto maybe_show_party_member_unlock_information(const RuntimeItemGrantRequest& request,
                                                        const RuntimeItemGrantApplyDeps& deps)
            -> void
        {
            if (!RuntimeGrantTokens::effective_show_info(request))
            {
                return;
            }

            constexpr std::wstring_view party_member_prefix = STR("Party Member: ");
            const std::wstring_view friend_name =
                request.item_name.starts_with(party_member_prefix)
                    ? std::wstring_view{request.item_name}.substr(party_member_prefix.size())
                    : std::wstring_view{request.item_name};

            RuntimeApMessageRequest message_request{};
            message_request.request_id =
                request.request_id.empty()
                    ? std::format(STR("party-member-unlock:{}"), request.index)
                    : std::format(STR("{}:party-member-unlock:{}"), request.request_id,
                                  request.index);
            message_request.message = std::format(STR("New friend unlocked: {}"), friend_name);
            message_request.item_name = request.item_name;
            message_request.location_name = request.location_name;
            message_request.duration_seconds = item_grant_message_duration_seconds;

            std::wstring failure_reason{};
            if (try_call_tresui_open_information(
                    message_request, deps.runtime_state, deps.tresui_open_information_path,
                    deps.tres_game_blueprint_library_default_object_path, &failure_reason))
            {
                Output::send<LogLevel::Verbose>(
                    STR("[KH3APProbeCpp] Party member unlock information "
                        "shown index={} item={}\n"),
                    request.index, request.item_name);
                return;
            }

            Output::send<LogLevel::Warning>(STR("[KH3APProbeCpp] Party member unlock information "
                                                "failed index={} item={} error={}\n"),
                                            request.index, request.item_name,
                                            failure_reason.empty() ? std::wstring{STR("unknown")}
                                                                   : failure_reason);
        }

        auto final_grant_safety_status(const RuntimeItemGrantRequest& request,
                                       UObject* world_context_object,
                                       const RuntimeItemGrantApplyDeps& deps)
            -> RuntimeGrantSafetyStatus
        {
            const RuntimeHotPathProfileScope profile{STR("runtime_item_grant:final_safety")};
            if (RuntimeSpaceService::is_space_active())
            {
                UObject* current = nullptr;
                auto status = runtime_space_inventory_safety_status(
                    deps.runtime_state,
                    space::supports_inventory_grant(request.grant_method, request.runtime_token),
                    &current);
                if (status.ready && current != world_context_object)
                {
                    return RuntimeGrantSafetyStatus{false, L"space_controller_changed", {}};
                }
                return status;
            }
            if (supports_alternate_game_over(request.action_kind, request.runtime_token,
                                             request.grant_method) &&
                world_context_object == character_swap::current_hero())
            {
                return runtime_alternate_game_over_safety_status(deps.runtime_state,
                                                                 world_context_object);
            }
            if (supports_alternate_bad_status(request.runtime_token, request.grant_method) &&
                world_context_object == character_swap::current_hero())
            {
                return runtime_alternate_bad_status_safety_status(deps.runtime_state,
                                                                  world_context_object);
            }
            if (auto* owner = persistent_player_reward_context();
                owner && owner == world_context_object)
            {
                if (!kh02rewards::supports_persistent_grant(request.grant_method))
                {
                    return {false, L"persistent_reward_method_not_supported", {}};
                }
                return runtime_persistent_reward_safety_status(
                        deps.runtime_state, owner, std::chrono::steady_clock::now());
            }
            // DeathLink must read the pawn's live state at execution time.
            // Airstepping can start after the cached idle/jump snapshot;
            // nullptr selects the existing live MyStateComp/current-state read.
            const bool requires_live_state =
                request.grant_method == STR("bp_tres_start_game_over") ||
                runtime_item_grant_is_bad_status_trap(request);
            const RuntimeGrantSafetyStatus general_status =
                runtime_grant_final_sora_state_safety_status(
                    deps.runtime_state, world_context_object,
                    requires_live_state ? nullptr : &deps.sora_current_state_cache);
            if (!general_status.ready || !runtime_item_grant_is_bad_status_trap(request))
            {
                return general_status;
            }
            return runtime_trap_application_state_status(world_context_object);
        }

        bool apply_manual_munny(const RuntimeItemGrantRequest& request,
                                UObject* world_context_object,
                                const RuntimeItemGrantApplyDeps& deps,
                                std::wstring& candidate_failure)
        {
            bool applied = false;
            if (request.munny_delta == 0 ||
                request.munny_delta < std::numeric_limits<int32_t>::min() ||
                request.munny_delta > std::numeric_limits<int32_t>::max())
            {
                candidate_failure =
                    std::format(STR("invalid manual munny delta={}"), request.munny_delta);
            }
            else
            {
                applied = try_call_bp_add_munny(
                    static_cast<int32_t>(request.munny_delta), deps.runtime_state,
                    deps.bp_add_munny_path, deps.tres_game_blueprint_library_default_object_path,
                    deps.debug_logging, &candidate_failure);
            }

            return applied;
        }

        bool apply_munny_pouch(const RuntimeItemGrantRequest& request,
                               UObject* world_context_object, const RuntimeItemGrantApplyDeps& deps,
                               std::wstring& candidate_failure)
        {
            bool applied = false;
            const int32_t munny_amount = munny_pouch_amount_for_token(request.runtime_token);
            if (munny_amount <= 0)
            {
                candidate_failure =
                    std::format(STR("unsupported add-munny token={}"), request.runtime_token);
            }
            else
            {
                applied = try_call_bp_tres_get_item_and_add_munny(
                    request.runtime_token, munny_amount, world_context_object,
                    RuntimeGrantTokens::effective_show_info(request), deps.runtime_state,
                    deps.bp_get_item_path, deps.bp_add_munny_path,
                    deps.tres_game_blueprint_library_default_object_path, deps.debug_logging,
                    &candidate_failure);
            }

            return applied;
        }

        bool apply_native_item(const RuntimeItemGrantRequest& request,
                               UObject* world_context_object, const RuntimeItemGrantApplyDeps& deps,
                               std::wstring& candidate_failure)
        {
            bool applied = false;
            if (persistent_player_reward_context() == world_context_object)
            {
                applied = try_call_persistent_inventory_grant(
                    request.runtime_token, world_context_object,
                    RuntimeGrantTokens::effective_show_info(request), &candidate_failure);
            }
            else
            {
                applied = try_call_bp_tres_get_item(
                    request.runtime_token, world_context_object,
                    RuntimeGrantTokens::effective_show_info(request), deps.runtime_state,
                    deps.bp_get_item_path, deps.tres_game_blueprint_library_default_object_path,
                    deps.debug_logging, &candidate_failure);
            }
            maybe_apply_starlight_story_flag_after_direct_item_grant(applied, request.runtime_token,
                                                                     request, deps);

            return applied;
        }

        bool apply_party_unlock(const RuntimeItemGrantRequest& request,
                                UObject* world_context_object,
                                const RuntimeItemGrantApplyDeps& deps,
                                std::wstring& candidate_failure)
        {
            bool applied = false;
            if (!deps.party_member_unlock)
            {
                candidate_failure = STR("party_member_unlock_service_unavailable");
            }
            else
            {
                applied = deps.party_member_unlock(
                    request.runtime_token,
                    RuntimeSpaceService::is_space_active() ||
                            world_context_object == persistent_player_reward_context()
                        ? nullptr : world_context_object,
                    &candidate_failure);
                if (applied)
                {
                    RuntimePortalService::notify_party_member_received();
                    maybe_show_party_member_unlock_information(request, deps);
                }
            }

            return applied;
        }

        bool apply_progressive_keyblade(const RuntimeItemGrantRequest& request,
                                        UObject* world_context_object,
                                        const RuntimeItemGrantApplyDeps& deps,
                                        std::wstring& candidate_failure)
        {
            bool applied = false;
            ProgressiveKeybladeLevelApplyResult progressive_result{};
            applied = try_apply_progressive_keyblade_level(
                request.runtime_token, deps.progressive_keyblade_item_manager_path,
                progressive_keyblade_refresh_is_forced(request), &progressive_result,
                &candidate_failure);
            if (applied)
            {
                Output::send<LogLevel::Verbose>(
                    STR("[KH3APProbeCpp] Progressive Keyblade Level applied index={} "
                        "target_level={} expected_flags={} already_set={} newly_set={} "
                        "refreshed={}\n"),
                    request.index, progressive_result.target_level,
                    progressive_result.expected_flag_count, progressive_result.already_set_count,
                    progressive_result.newly_set_count,
                    progressive_result.item_manager_refreshed ? STR("true") : STR("false"));
                maybe_show_progressive_keyblade_level_information(request, progressive_result,
                                                                  deps);
                if (progressive_result.newly_set_count > 0 &&
                    deps.progressive_keyblade_level_applied)
                {
                    deps.progressive_keyblade_level_applied();
                }
            }

            return applied;
        }

        bool apply_ultima_shard(const RuntimeItemGrantRequest& request,
                                UObject* world_context_object,
                                const RuntimeItemGrantApplyDeps& deps,
                                std::wstring& candidate_failure)
        {
            bool applied = false;
            const auto parsed = ultima_weapon_shard::parse(request.runtime_token);
            if (!parsed)
            {
                candidate_failure = ultima_weapon_shard::failure_reason(parsed.failure);
                applied = false;
            }
            else
            {
                applied = true;
                if (parsed.shard.awards_weapon())
                {
                    // Grant the unique weapon first so a transient failure on
                    // the material grant cannot duplicate a shard on retry.
                    applied = try_call_bp_tres_get_item(
                        s_ultima_weapon_runtime_token, world_context_object,
                        RuntimeGrantTokens::effective_show_info(request), deps.runtime_state,
                        deps.bp_get_item_path, deps.tres_game_blueprint_library_default_object_path,
                        deps.debug_logging, &candidate_failure);
                }
                if (applied)
                {
                    applied = try_call_bp_tres_get_item(
                        s_ultima_weapon_shard_inventory_runtime_token, world_context_object,
                        RuntimeGrantTokens::effective_show_info(request), deps.runtime_state,
                        deps.bp_get_item_path, deps.tres_game_blueprint_library_default_object_path,
                        deps.debug_logging, &candidate_failure);
                }
            }

            return applied;
        }

        bool apply_grant_to_candidate(const RuntimeItemGrantRequest& request,
                                      UObject* world_context_object,
                                      const RuntimeItemGrantApplyDeps& deps,
                                      std::wstring& candidate_failure)
        {
            bool applied = false;
            if (request.grant_method == STR("bp_add_munny"))
            {
                applied =
                    apply_manual_munny(request, world_context_object, deps, candidate_failure);
            }
            else if (request.grant_method == STR("bp_tres_get_item_add_munny"))
            {
                applied = apply_munny_pouch(request, world_context_object, deps, candidate_failure);
            }
            else if (request.grant_method == STR("bp_tres_get_item"))
            {
                applied = apply_native_item(request, world_context_object, deps, candidate_failure);
            }
            else if (request.grant_method == STR("controller_unlock"))
            {
                RuntimeControllerAbilityGrantResult controller_result{};
                applied = deps.controller_unlock_deps.controller_abilities.apply_unlock_item(
                    request.runtime_token, world_context_object, &controller_result,
                    &candidate_failure);
                if (applied)
                {
                    maybe_present_controller_unlock_ability(request, controller_result,
                                                            world_context_object, deps);
                }
            }
            else if (request.grant_method == STR("character_unlock"))
            {
                // Ownership is reconstructed from the seed's AP receipt catalog,
                // independently of vanilla inventory and save/retry checkpoints.
                applied = character_unlocks::token_bit(request.runtime_token) != 0;
                if (!applied)
                {
                    candidate_failure = STR("unknown_character_unlock_token");
                }
            }
            else if (request.grant_method == STR("party_member_unlock"))
            {
                applied =
                    apply_party_unlock(request, world_context_object, deps, candidate_failure);
            }
            else if (request.grant_method == STR("keyblade_style"))
            {
                applied = try_set_keyblade_style(deps.runtime_state, world_context_object,
                                                 request.runtime_token,
                                                 RuntimeGrantTokens::effective_show_info(request) &&
                                                     !RuntimeSpaceService::is_space_active(),
                                                 &candidate_failure);
            }
            else if (request.grant_method == STR("progressive_keyblade_level"))
            {
                applied = apply_progressive_keyblade(request, world_context_object, deps,
                                                     candidate_failure);
            }
            else if (request.grant_method == STR("ultima_weapon_shard"))
            {
                applied =
                    apply_ultima_shard(request, world_context_object, deps, candidate_failure);
            }
            else if (request.grant_method == STR("bp_tres_start_game_over"))
            {
                applied = try_call_bp_tres_start_game_over(
                    world_context_object, false, deps.runtime_state,
                    deps.bp_tres_start_game_over_path,
                    deps.tres_game_blueprint_library_default_object_path, deps.debug_logging,
                    &candidate_failure);
            }
            else if (request.grant_method == king_mickey_trap_method &&
                     request.runtime_token == king_mickey_trap_token)
            {
                applied =
                    character_swap::start_king_mickey_trap(world_context_object, request.index);
                if (!applied)
                {
                    candidate_failure = L"king_mickey_trap_waiting";
                }
            }
            else if (request.grant_method == STR("direct_player_bad_status"))
            {
                applied =
                    try_apply_direct_player_bad_status(request.runtime_token, world_context_object,
                                                       deps.runtime_state, &candidate_failure);
            }
            else if (request.grant_method == STR("probe_sora_normal_form_barrier_inject"))
            {
                applied = try_inject_runtime_sora_normal_form_barrier(world_context_object,
                                                                      &candidate_failure);
            }
            else if (request.grant_method == STR("probe_sora_normal_form_barrier_remove"))
            {
                applied = try_remove_runtime_sora_normal_form_barrier(world_context_object,
                                                                      &candidate_failure);
            }
            else if (request.grant_method == STR("durable_sora_ability"))
            {
                if (RuntimeGrantTokens::classify(request.runtime_token) !=
                    RuntimeGrantTokenFamily::Ability)
                {
                    candidate_failure = STR("durable_sora_ability_requires_ability_token");
                }
                else
                {
                    applied = try_call_durable_sora_ability_grant(
                        request, world_context_object, deps.runtime_state, &candidate_failure,
                        deps.auto_equip_movement_abilities);
                }
            }
            else if (request.grant_method == STR("durable_sora_vbonus"))
            {
                if (RuntimeGrantTokens::classify(request.runtime_token) !=
                    RuntimeGrantTokenFamily::VictoryBonus)
                {
                    candidate_failure = STR("durable_sora_vbonus_requires_victory_bonus_token");
                }
                else
                {
                    applied = try_call_durable_sora_vbonus_grant(
                        request, world_context_object, deps.runtime_state, &candidate_failure);
                }
            }
            else
            {
                candidate_failure =
                    std::format(STR("unsupported grant_method={}"), request.grant_method);
            }

            return applied;
        }

    } // namespace

    auto make_runtime_item_grant_apply_deps(
        RuntimeState& runtime_state, std::wstring_view bp_get_item_path,
        std::wstring_view bp_add_munny_path, std::wstring_view set_game_flag_boolean_path,
        std::wstring_view bp_tres_start_game_over_path,
        std::wstring_view tres_game_blueprint_library_default_object_path,
        std::wstring_view tresui_open_information_path,
        std::wstring_view progressive_keyblade_item_manager_path,
        std::wstring_view runtime_context_summary, bool debug_logging,
        RuntimeControllerUnlockGrantDeps controller_unlock_deps,
        RuntimeSoraCurrentStateCacheView sora_current_state_cache) -> RuntimeItemGrantApplyDeps
    {
        return {runtime_state,
                bp_get_item_path,
                bp_add_munny_path,
                set_game_flag_boolean_path,
                bp_tres_start_game_over_path,
                tres_game_blueprint_library_default_object_path,
                tresui_open_information_path,
                progressive_keyblade_item_manager_path,
                runtime_context_summary,
                debug_logging,
                controller_unlock_deps,
                {},
                std::move(sora_current_state_cache),
                {}};
    }

    auto try_apply_runtime_item_grant_request(const RuntimeItemGrantRequest& request,
                                              const std::vector<UObject*>& world_context_candidates,
                                              const RuntimeItemGrantApplyDeps& deps,
                                              RuntimeItemApplyPayload* payload,
                                              std::wstring* failure_details,
                                              UObject** applied_world_context_object,
                                              bool* unsafe_abort_requested) -> bool
    {
        if (payload)
        {
            *payload = RuntimeItemApplyPayload{};
        }
        if (applied_world_context_object)
        {
            *applied_world_context_object = nullptr;
        }
        if (failure_details)
        {
            failure_details->clear();
        }
        if (unsafe_abort_requested)
        {
            *unsafe_abort_requested = false;
        }

        for (UObject* const world_context_object : world_context_candidates)
        {
            std::wstring candidate_failure{};
            const auto final_safety_status =
                final_grant_safety_status(request, world_context_object, deps);
            if (!final_safety_status.ready)
            {
                candidate_failure = std::format(
                    STR("{}:{}"), final_safety_status.reason,
                    truncate_for_log(final_safety_status.details, grant_safety_log_chars));
                if (failure_details)
                {
                    *failure_details = candidate_failure;
                }
                Output::send<LogLevel::Verbose>(
                    STR("[KH3APProbeCpp] Runtime item final grant gate blocked index={} item={} "
                        "token={} method={} reason={} details={} world={}\n"),
                    request.index, request.item_name, request.runtime_token, request.grant_method,
                    final_safety_status.reason,
                    truncate_for_log(final_safety_status.details, failed_grant_safety_log_chars),
                    format_opaque_world_context_address(world_context_object));
                if (unsafe_abort_requested)
                {
                    *unsafe_abort_requested = true;
                }
                return false;
            }

            const bool applied =
                apply_grant_to_candidate(request, world_context_object, deps, candidate_failure);

            if (!applied)
            {
                if (failure_details)
                {
                    *failure_details = candidate_failure.empty()
                                           ? std::wstring{STR("grant_failed")}
                                           : truncate_for_log(candidate_failure,
                                                              grant_candidate_failure_log_chars);
                }
                continue;
            }

            if (payload)
            {
                *payload = RuntimeGrantTokens::build_apply_payload(request);
            }
            if (request.runtime_token == L"ETresAbilityKind::GLIDE")
            {
                // Direct durable ability grants bypass the native add hook.
                // Wake Kairi's shared unlock refresh after a successful receipt.
                deps.controller_unlock_deps.controller_abilities.request_refresh(RuntimeControllerAbilityEvent::Grant);
            }
            const bool show_info = RuntimeGrantTokens::effective_show_info(request);
            // Blueprint stock also drives travel and the lock panel. Refresh
            // it after Sora grants and silent save replays, independently of
            // whether a space item banner is shown.
            if (request.runtime_token.starts_with(L"GUMISHIPBP_ITEM") ||
                (show_info && RuntimeSpaceService::is_space_active()))
            {
                RuntimeSpaceService::notify_item_received(request.runtime_token, show_info);
            }
            if (applied_world_context_object)
            {
                *applied_world_context_object = world_context_object;
            }
            return true;
        }

        return false;
    }
} // namespace kh3ap
