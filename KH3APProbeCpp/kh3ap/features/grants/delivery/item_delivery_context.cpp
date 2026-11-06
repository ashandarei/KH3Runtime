#include "kh3ap/app/config.h"
#include "kh3ap/features/player/characters/character_swap.h"
#include "kh3ap/features/player/objects/object_resolver.h"
#include "kh3ap/bridge/transport/bridge_channels.h"
#include "kh3ap/features/grants/delivery/item_delivery_internal.h"
#include "kh3ap/features/world/space/space_service.h"
#include "kh3ap/features/grants/grant_state.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/app/state.h"
#include "kh3ap/diagnostics/ui_diagnostic_state.h"

#include "kh3ap/features/kh02/runtime/kh02_world_context.h"

#include <UnrealDef.hpp>
#include <array>
#include <format>
#include <utility>

namespace kh3ap::item_delivery
{
    using ::kh3ap::settings::s_enable_runtime_item_grant_candidate_recovery;
    using ::kh3ap::settings::s_enable_runtime_item_grant_playable_safety;
    using ::kh3ap::settings::s_runtime_item_grant_ready_context_debounce;

    using namespace RC;
    using namespace RC::Unreal;

    auto requires_persistent_delivery_context() -> bool
    {
        const bool kh02_world = kh02_current_story_world() != nullptr;
        const bool kh02_possessed_sora = kh02_world &&
            RuntimeObjectResolver::is_sora_ability_pawn_candidate(kh02_story_player(nullptr));
        return runtime_item_delivery_requires_persistent_context(
            character_swap::current_hero() != nullptr, character_swap::handoff_pending(),
            kh02_world, kh02_possessed_sora);
    }

    auto prepare_delivery_context(
            const RuntimeItemGrantPollDeps& deps,
            const RuntimeItemDeliveryPolicy& policy,
            bool is_batch,
            std::chrono::steady_clock::time_point now) -> DeliveryContext
    {
        const bool allow_special_battle_game_over = policy.allow_special_battle_game_over;
        const bool allow_during_special_battle =
                allow_special_battle_game_over || policy.allow_special_battle_bad_status_trap;
        const bool contains_bad_status_trap = policy.contains_bad_status_trap;
        auto* alternate = policy.supports_alternate_game_over ? character_swap::game_over_context() :
            policy.supports_alternate_bad_status ? character_swap::bad_status_context() : nullptr;
        // The story hero selects the route only. Sora still resolves through the
        // normal cached-pawn, possession, loading, battle and live-state gates below.
        if (!alternate && requires_persistent_delivery_context())
        {
            auto* owner = persistent_player_reward_context();
            if (!policy.supports_kh02_persistent_rewards || !owner)
            {
                return {{false, L"persistent_reward_context_not_ready", {}}, {}};
            }
            auto safety = runtime_persistent_reward_safety_status(deps.runtime_state, owner, now);
            std::vector<UObject*> candidates;
            if (safety.ready)
            {
                candidates.push_back(owner);
            }
            return {std::move(safety), std::move(candidates)};
        }
        RuntimeGrantSafetyStatus safety_status = [&]() {
            const RuntimeHotPathProfileScope profile{
                    is_batch ? STR("runtime_item_grant:batch_snapshot_safety")
                             : STR("runtime_item_grant:single_snapshot_safety")};
            if (alternate)
            {
                if (policy.supports_alternate_game_over)
                {
                    return runtime_alternate_game_over_safety_status(deps.runtime_state, alternate);
                }
                return runtime_alternate_bad_status_safety_status(deps.runtime_state, alternate);
            }
            return runtime_grant_context_snapshot_safety_status(
                    deps.runtime_state,
                    now,
                    s_runtime_item_grant_ready_context_debounce,
                    allow_during_special_battle,
                    allow_special_battle_game_over);
        }();
        std::vector<UObject*> world_context_candidates{};
        const RuntimeGrantSafetyStatus unsafe_map_status = [&]() {
            const RuntimeHotPathProfileScope profile{
                    is_batch ? STR("runtime_item_grant:batch_unsafe_map_status")
                             : STR("runtime_item_grant:single_unsafe_map_status")};
            return current_runtime_unsafe_map_status(deps.runtime_state, allow_special_battle_game_over);
        }();
        const RuntimeGrantSafetyStatus special_battle_mode_status = [&]() {
            const RuntimeHotPathProfileScope profile{
                    is_batch ? STR("runtime_item_grant:batch_special_battle_status")
                             : STR("runtime_item_grant:single_special_battle_status")};
            if (allow_during_special_battle)
            {
                return RuntimeGrantSafetyStatus{
                        .ready = true,
                        .reason = std::wstring{STR("ready")},
                        .details = std::wstring{},
                };
            }
            return current_runtime_special_battle_mode_status(deps.runtime_state.grant_state(), now);
        }();
        const RuntimeGrantSafetyStatus bistro_return_gate_status = [&]() {
            const RuntimeHotPathProfileScope profile{
                    is_batch ? STR("runtime_item_grant:batch_bistro_gate_status")
                             : STR("runtime_item_grant:single_bistro_gate_status")};
            return current_runtime_bistro_return_gate_status(deps.runtime_state.grant_state(), now);
        }();
        const RuntimeGrantSafetyStatus classic_kingdom_transition_gate_status = [&]() {
            const RuntimeHotPathProfileScope profile{
                    is_batch ? STR("runtime_item_grant:batch_classic_kingdom_gate_status")
                             : STR("runtime_item_grant:single_classic_kingdom_gate_status")};
            return current_runtime_classic_kingdom_transition_gate_status(
                    deps.runtime_state.grant_state(),
                    now);
        }();
        const RuntimeGrantSafetyStatus olympus_coliseum_transition_gate_status = [&]() {
            const RuntimeHotPathProfileScope profile{
                    is_batch ? STR("runtime_item_grant:batch_olympus_coliseum_gate_status")
                             : STR("runtime_item_grant:single_olympus_coliseum_gate_status")};
            return current_runtime_olympus_coliseum_transition_gate_status(
                    deps.runtime_state.grant_state(),
                    now);
        }();
        const RuntimeGrantSafetyStatus sora_state_cache_status = [&]() {
            const RuntimeHotPathProfileScope profile{
                    is_batch ? STR("runtime_item_grant:batch_sora_state_cache_status")
                             : STR("runtime_item_grant:single_sora_state_cache_status")};
            if (alternate)
            {
                return current_runtime_trap_live_state_status(alternate);
            }
            const RuntimeGrantSafetyStatus general_status =
                    current_runtime_sora_state_cache_status(deps.apply_deps.sora_current_state_cache);
            if (!general_status.ready || !contains_bad_status_trap)
            {
                return general_status;
            }
            return current_runtime_bad_status_trap_sora_state_cache_status(
                    deps.apply_deps.sora_current_state_cache);
        }();
        const std::array gates{
                unsafe_map_status, special_battle_mode_status, bistro_return_gate_status,
                classic_kingdom_transition_gate_status, olympus_coliseum_transition_gate_status,
                sora_state_cache_status};
        auto decision = evaluate_runtime_item_delivery_gates(
                std::move(safety_status), gates, policy.require_fresh_playable_context);
        safety_status = std::move(decision.status);
        if (alternate)
        {
            if (safety_status.ready)
            {
                world_context_candidates = {alternate};
            }
        }
        else if (decision.recover_cached_pawn)
        {
            // Global gates must permit recovery before touching cached pawn candidates.
            if constexpr (!s_enable_runtime_item_grant_candidate_recovery)
            {
                safety_status = {
                        .ready = false,
                        .reason = std::wstring{STR("candidate_recovery_disabled")},
                        .details = std::wstring{STR("runtime item grant candidate recovery disabled by isolation build")},
                };
            }
            else
            {
                const std::vector<UObject*> pawn_candidates = [&]() {
                    const RuntimeHotPathProfileScope profile{
                            is_batch ? STR("runtime_item_grant:batch_pawn_candidates")
                                     : STR("runtime_item_grant:single_pawn_candidates")};
                    return collect_cached_runtime_grant_pawn_candidates(deps.runtime_state, nullptr);
                }();
                if (pawn_candidates.empty())
                {
                    safety_status = {
                            .ready = false,
                            .reason = std::wstring{STR("no_cached_player_pawn")},
                            .details = std::wstring{STR("collect_cached_runtime_grant_pawn_candidates returned empty")},
                    };
                }
                else if constexpr (!s_enable_runtime_item_grant_playable_safety)
                {
                    safety_status = {
                            .ready = false,
                            .reason = std::wstring{STR("playable_safety_disabled")},
                            .details = std::format(
                                    STR("runtime item grant playable safety disabled by isolation build; candidates={}"),
                                    static_cast<uint64_t>(pawn_candidates.size())),
                    };
                }
                else
                {
                    safety_status = [&]() {
                        const RuntimeHotPathProfileScope profile{
                                is_batch ? STR("runtime_item_grant:batch_playable_safety")
                                         : STR("runtime_item_grant:single_playable_safety")};
                        return runtime_grant_playable_safety_status(
                                deps.runtime_state.ui_diagnostic_state().blocking_ui_state(),
                                deps.runtime_state.grant_state().pawn_state(),
                                pawn_candidates,
                                now);
                    }();
                }
            }
        }
        if (RuntimeSpaceService::is_space_active())
        {
            // Space has its own possession/loading/UI gate and never populates
            // the cached playable-Sora context.
            UObject* controller = nullptr;
            safety_status = runtime_space_inventory_safety_status(
                    deps.runtime_state, policy.supports_space_inventory, &controller);
            world_context_candidates.clear();
            if (safety_status.ready)
            {
                world_context_candidates.push_back(controller);
            }
        }
        return {std::move(safety_status), std::move(world_context_candidates)};
    }

    auto collect_delivery_world_candidates(const RuntimeItemGrantPollDeps& deps, bool is_batch)
            -> std::vector<UObject*>
    {
        const RuntimeHotPathProfileScope profile{
                is_batch ? STR("runtime_item_grant:batch_world_candidates") : STR("runtime_item_grant:single_world_candidates")};
        return collect_fast_runtime_item_grant_world_context_candidates(deps.runtime_state, nullptr);
    }
}
