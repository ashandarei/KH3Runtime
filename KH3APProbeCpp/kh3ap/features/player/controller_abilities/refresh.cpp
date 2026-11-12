#include "kh3ap/app/config.h"
#include "kh3ap/features/player/characters/character_swap.h"
#include "kh3ap/features/player/combat/aqua_style_unlock.h"
#include "kh3ap/features/player/controller_abilities/host.h"

#include "kh3ap/bridge/transport/bridge_channels.h"
#include "kh3ap/app/state.h"
#include "kh3ap/core/conditional_profile_scope.h"
#include "kh3ap/core/objects/uobject_weak.h"
#include "kh3ap/features/grants/vbonus/vbonus_grant_call_helpers.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/features/kh02/story/kh02_story.h"
#include <DynamicOutput/DynamicOutput.hpp>
#include <Unreal/UObject.hpp>
#include <chrono>
#include <array>
#include <string>

namespace kh3ap
{
    using namespace RC;
    using namespace RC::Unreal;
    using namespace controller_ability_detail;
    namespace
    {
        using ::kh3ap::settings::s_enable_runtime_hot_path_profiling;
    }
    auto RuntimeControllerAbilityService::Impl::request_refresh(RuntimeControllerAbilityEvent event)
        -> void
    {
        // Possession already reconciles ordinary command lists. The late
        // banner is only a retry signal after failed setup, plus Olympus'
        // explicit story gate override. Native ability-change hooks remain
        // independent and always enqueue their own work.
        if (event == RuntimeControllerAbilityEvent::MapReady &&
            successful_pawn_.Get() == config_.runtime_state.grant_state().pawn_state().object() &&
            successful_pawn_.Get() != nullptr && successful_controller_.Get() != nullptr &&
            successful_controller_.Get() ==
                config_.runtime_state.grant_state().pawn_state().controller_object() &&
            !is_runtime_mount_olympus_ability_context(
                config_.runtime_state.grant_state().pawn_state().name()))
        {
            return;
        }
        pending_events_.fetch_or(static_cast<uint32_t>(event));
    }

    auto RuntimeControllerAbilityService::Impl::process_pending_refresh(
        std::chrono::steady_clock::time_point now) -> void
    {
        const auto style_revision = aqua_style_unlock::revision.load(std::memory_order_acquire);
        if (style_revision != aqua_style_revision_)
        {
            aqua_style_revision_ = style_revision;
            request_refresh(RuntimeControllerAbilityEvent::Grant);
        }
        const uint32_t events = pending_events_.exchange(0);
        if (!refresh_state_.begin(now, events))
        {
            return;
        }
        if ((events & static_cast<uint32_t>(RuntimeControllerAbilityEvent::MapReady)) != 0)
        {
            map_ready_pending_ = true;
            mount_olympus_shoot_flow_pending_ = false;
        }
        RuntimeConditionalProfileScope<s_enable_runtime_hot_path_profiling> profile{
            STR("controller_abilities:refresh")};
        // Menu/native notifications compare before/after effective ability
        // state. Preserve the existing full refresh following an AP grant.
        // Pawn replacement invalidates applied effects even when the same
        // controller survives Retry and ownership has not changed.
        if ((events & (static_cast<uint32_t>(RuntimeControllerAbilityEvent::Grant) |
                       static_cast<uint32_t>(RuntimeControllerAbilityEvent::PawnChanged))) != 0)
        {
            applied_states_.reset();
        }

        const auto refresh = [&]() -> bool
        {
            bool complete = true;
            if (!ability_enum_augmentation_done_)
            {
                ability_enum_augmentation_done_ = try_apply_ability_enum_augmentation();
                complete = ability_enum_augmentation_done_;
            }
            try
            {
                complete = ensure_portal_ownership_binding() && complete;
            }
            catch (...)
            {
                if (!portal_binding_warning_)
                {
                    Output::send<LogLevel::Warning>(
                        STR("[KH3APProbeCpp] Portal ownership binding unavailable; controller "
                            "ability refresh continues\n"));
                }
                portal_binding_warning_ = true;
                complete = false;
            }

            if (auto* aqua = persistent_player_reward_context())
            {
                std::wstring failure;
                const bool refreshed = refresh_shared_aqua_unlocks(aqua, &failure);
                if (!refreshed && !failure.empty() && failure != last_query_failure_)
                {
                    Output::send<LogLevel::Warning>(
                        STR("[KH3APProbeCpp] Shared character ability refresh unavailable failure={}\n"),
                        failure);
                }
                last_query_failure_ = refreshed ? std::wstring{} : failure;
                return refreshed && complete;
            }
            UObject* const pawn = config_.runtime_state.grant_state().pawn_state().object();
            UObject* const controller = resolve_controller();
            if (!pawn || !kh3ap::is_uobject_valid_fast(pawn) || !controller)
            {
                return false;
            }
            observe_controller(controller);

            const bool in_mount_olympus = is_runtime_mount_olympus_ability_context(
                config_.runtime_state.grant_state().pawn_state().name());
            if (map_ready_pending_)
            {
                map_ready_pending_ = false;
                if (applied_states_.reset_for_map_ready(
                        config_.runtime_state.grant_state().pawn_state().name()))
                {
                    // EW and Olympus setup can reuse a controller while
                    // overwriting its gates. Read owned/equipped state again
                    // and apply it even when the last-applied cache agrees.
                    mount_olympus_shoot_flow_pending_ = true;
                }
            }

            std::array<RuntimeSoraAbilityEntryState, s_refresh_abilities.size()> states{};
            std::wstring failure{};
            const bool state_available = try_read_runtime_sora_ability_entry_states(
                s_refresh_abilities, pawn, states, &failure);
            if (!state_available)
            {
                if (config_.debug_logging && failure != last_query_failure_)
                {
                    Output::send<LogLevel::Verbose>(
                        STR("[KH3APProbeCpp] Controller ability refresh unavailable failure={}\n"),
                        failure);
                }
                last_query_failure_ = failure;
                complete = false;
            }
            else
            {
                last_query_failure_.clear();
                for (size_t index = 0; index < s_controller_abilities.size(); ++index)
                {
                    const auto& definition = s_controller_abilities[index];
                    const auto& state = states[index];
                    const bool desired = state.owned && state.equipped;
                    if (definition.ability_value == ability_id::magic)
                    {
                        continue; // shared MP, separate commands below
                    }
                    // Per-keyblade gates are owned by the style service.
                    if (definition.ability_value == ability_id::style_change &&
                        config_.style_change_mode &&
                        config_.style_change_mode() != RuntimeStyleChangeMode::Shared)
                    {
                        continue;
                    }
                    if (!setter_needs_update(definition.setter_index, desired))
                    {
                        continue;
                    }
                    if (call_setter(controller, definition.setter_index, desired, &failure))
                    {
                        applied_states_.record(definition.setter_index, desired);
                        last_setter_failure_.clear();
                    }
                    else
                    {
                        complete = false;
                        if (config_.debug_logging && failure != last_setter_failure_)
                        {
                            Output::send<LogLevel::Warning>(
                                STR("[KH3APProbeCpp] Controller ability refresh setter failed "
                                    "ability={} failure={}\n"),
                                definition.ability_value, failure);
                        }
                        last_setter_failure_ = failure;
                    }
                }
                const auto& magic = states[s_magic_refresh_index];
                const auto& links = states.back();
                complete = apply_magic_links(controller, pawn, magic.owned && magic.equipped,
                                             links.owned && links.equipped, &failure) && complete;
                // Reconcile deferred space/checkpoint command lists only at
                // lifecycle boundaries, not whenever Customize is closed.
                constexpr auto command_lifecycle_events =
                    static_cast<uint32_t>(RuntimeControllerAbilityEvent::Startup) |
                    static_cast<uint32_t>(RuntimeControllerAbilityEvent::MapReady) |
                    static_cast<uint32_t>(RuntimeControllerAbilityEvent::PawnChanged);
                if ((refresh_state_.events() & command_lifecycle_events) != 0)
                {
                    complete = try_refresh_runtime_sora_magic_commands(pawn, &failure) && complete;
                    complete = try_refresh_runtime_sora_link_commands(pawn, config_.runtime_state,
                                                                      &failure) &&
                               complete;
                }
            }
            // Airstep is baseline movement, including for existing seeds
            // that have not delivered the legacy unlock item. Reassert it
            // on each event refresh because story setters can clear it
            // without changing the controller or our last-applied cache.
            {
                if (call_setter(controller, setter_index::airstep, true, &failure))
                {
                    applied_states_.record(setter_index::airstep, true);
                }
                else
                {
                    complete = false;
                }
            }
            if (mount_olympus_shoot_flow_pending_)
            {
                // ShootFlow is a baseline controller gate, not an AP
                // ability-list unlock. Magic/StyleChange above remain gated
                // by their actual owned && equipped ability entries.
                if (!in_mount_olympus ||
                    call_setter(controller, setter_index::shoot_flow, true, &failure))
                {
                    mount_olympus_shoot_flow_pending_ = false;
                }
                else
                {
                    complete = false;
                }
            }
            return complete;
        };
        const bool success = refresh();
        const auto reasons = refresh_state_.events();
        refresh_state_.finish(success);
        if (success)
        {
            successful_pawn_ =
                make_uobject_weak(config_.runtime_state.grant_state().pawn_state().object());
            successful_controller_ = make_uobject_weak(
                config_.runtime_state.grant_state().pawn_state().controller_object());
            Output::send<LogLevel::Verbose>(STR("[KH3APProbeCpp] Controller ability event refresh "
                                                "complete events={} attempts={}\n"),
                                            reasons, refresh_state_.attempts());
        }
        else
        {
            successful_pawn_ = nullptr;
        }
        if (!success && !refresh_state_.pending())
        {
            Output::send<LogLevel::Warning>(
                STR("[KH3APProbeCpp] Controller ability event refresh deferred until next event "
                    "events={} attempts={}\n"),
                reasons, refresh_state_.attempts());
        }
    }
} // namespace kh3ap
