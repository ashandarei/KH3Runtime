#include "kh3ap/features/player/characters/character_swap.h"
#include "kh3ap/bridge/transport/bridge_channels.h"
#include "kh3ap/features/moogle_shop/workshop_events.h"
#include "kh3ap/features/grants/delivery/item_delivery_internal.h"
#include "kh3ap/features/grants/delivery/item_delivery_limits.h"
#include "kh3ap/features/world/space/space_service.h"
#include "kh3ap/features/grants/abilities/ability_diagnostic_state.h"
#include "kh3ap/bridge/transport/bridge_paths.h"
#include "kh3ap/bridge/transport/bridge_io_helpers.h"
#include "kh3ap/features/grants/grant_state.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/app/state.h"
#include "kh3ap/diagnostics/ui_diagnostic_state.h"
#include "kh3ap/bridge/utilities.h"
#include "kh3ap/bridge/serialization/json_fields.h"
#include "kh3ap/bridge/serialization/request_parser.h"
#include "kh3ap/bridge/transport/pipe_transport.h"
#include "kh3ap/bridge/rewards/reconciliation.h"

#include "kh3ap/features/kh02/story/kh02_story.h"
#include "kh3ap/features/grants/grant_tokens.h"
#include "kh3ap/features/grants/vbonus/vbonus_grant_call_helpers.h"
#include "kh3ap/features/player/controller_abilities/controller_ability_service.h"
#include "kh3ap/features/kh02/rewards/kh02_reward_policy.h"

#include <DynamicOutput/DynamicOutput.hpp>
#include <UnrealDef.hpp>
#include <chrono>
#include <format>
#include <utility>

namespace kh3ap
{
    namespace
    {
        constexpr auto item_grant_wait_log_interval = std::chrono::seconds(1);
        constexpr auto item_grant_poll_log_interval = std::chrono::seconds(5);
        constexpr auto item_grant_stale_key_age = std::chrono::seconds(5);
    }

    using namespace RC;
    using namespace RC::Unreal;
    using namespace item_delivery;

    auto make_runtime_item_grant_post_apply_observer_deps(
            RuntimeState& runtime_state,
            bool runtime_ability_state_probe_enabled,
            bool targeted_runtime_ability_query_probe_enabled,
            std::wstring_view runtime_context_summary,
            std::array<std::pair<std::wstring_view, std::wstring_view>, 3> targeted_ability_query_functions,
            AbilityStateSnapshotEmitter ability_snapshot_emitter,
            AbilityProbeJsonlEmitter targeted_ability_probe_jsonl_emitter,
            RuntimeLiveAbilityOwnerProbeEmitter live_ability_owner_probe_emitter) -> RuntimeItemGrantPostApplyObserverDeps
    {
        return {
                runtime_state,
                runtime_ability_state_probe_enabled,
                targeted_runtime_ability_query_probe_enabled,
                runtime_context_summary,
                targeted_ability_query_functions,
                ability_snapshot_emitter,
                targeted_ability_probe_jsonl_emitter,
                live_ability_owner_probe_emitter};
    }

    auto make_runtime_item_grant_poll_deps(
            RuntimeState& runtime_state,
            std::optional<std::filesystem::path> bridge_dir,
            std::wstring_view runtime_item_grant_filename,
            std::wstring_view runtime_item_grant_priority_filename,
            std::wstring_view runtime_item_apply_filename,
            std::wstring_view protocol_name,
            int64_t protocol_version,
            std::wstring_view runtime_context_summary,
            std::chrono::steady_clock::duration runtime_item_grant_delay,
            std::chrono::steady_clock::duration slow_wait_log_delay,
            bool runtime_context_object_not_real,
            RuntimeItemGrantApplyDeps apply_deps,
            RuntimeItemGrantPostApplyObserverDeps post_apply_observer_deps) -> RuntimeItemGrantPollDeps
    {
        return {
                runtime_state,
                std::move(bridge_dir),
                runtime_item_grant_filename,
                runtime_item_grant_priority_filename,
                runtime_item_apply_filename,
                protocol_name,
                protocol_version,
                runtime_context_summary,
                runtime_item_grant_delay,
                slow_wait_log_delay,
                runtime_context_object_not_real,
                apply_deps,
                post_apply_observer_deps};
    }

    auto observe_runtime_item_grant_applied(
            const RuntimeItemGrantRequest& request,
            UObject* applied_world_context_object,
            bool is_batch,
            const RuntimeItemGrantPostApplyObserverDeps& deps) -> void
    {
        notify_workshop_grant_applied(request.grant_method,request.runtime_token);
        if (applied_world_context_object && persistent_player_reward_context() == applied_world_context_object)
        {
            if (RuntimeGrantTokens::effective_show_info(request) &&
                (request.action_kind == L"runtime_item_grant_batch" || request.action_kind == L"runtime_item_grant"))
            {
                const auto ability_token = request.grant_method == L"controller_unlock"
                        ? std::wstring{runtime_controller_unlock_ability_token(request.runtime_token)}
                        : request.grant_method == L"durable_sora_ability" ? request.runtime_token : std::wstring{};
                std::wstring failure;
                const bool popup_shown = !ability_token.empty() && try_present_native_vbonus_hud_ability_tokens(
                        {ability_token}, applied_world_context_object, deps.runtime_state, &failure);
                if (!kh02rewards::uses_native_ability_popup(request.grant_method) &&
                    request.grant_method != L"bp_tres_get_item" &&
                    request.grant_method != L"bp_tres_get_item_add_munny")
                {
                    reconciliation::persistent_reward_notice(request, reconciliation::epoch());
                }
                Output::send<LogLevel::Verbose>(STR("[KH3APProbeCpp] Aqua reward presentation index={} item={} native_ability_popup={} failure={}\n"),
                        request.index, request.item_name, popup_shown, failure);
            }
            // Sora-only owner diagnostics must not inspect Aqua as Sora.
            return;
        }
        if (!request.runtime_token.starts_with(STR("ETresAbilityKind::")))
        {
            return;
        }

        if (is_batch)
        {
            if (deps.runtime_ability_state_probe_enabled)
            {
                if (deps.ability_snapshot_emitter)
                {
                    deps.ability_snapshot_emitter(
                            std::format(STR("post_runtime_batch_grant:index={}:{}"), request.index, request.runtime_token),
                            applied_world_context_object);
                }
            }
            if (deps.targeted_runtime_ability_query_probe_enabled)
            {
                log_targeted_runtime_ability_query_snapshot(
                        std::format(STR("post_runtime_batch_grant:index={}"), request.index),
                        request.runtime_token,
                        applied_world_context_object,
                        deps.runtime_state,
                        deps.targeted_runtime_ability_query_probe_enabled,
                        deps.runtime_context_summary,
                        deps.targeted_ability_query_functions,
                        deps.targeted_ability_probe_jsonl_emitter);
            }
            if (deps.live_ability_owner_probe_emitter)
            {
                deps.live_ability_owner_probe_emitter(
                        std::format(STR("post_runtime_batch_grant:index={}:{}"), request.index, request.runtime_token),
                        applied_world_context_object);
            }
            return;
        }

        if (deps.targeted_runtime_ability_query_probe_enabled)
        {
            log_targeted_runtime_ability_query_snapshot(
                    std::format(STR("post_runtime_grant:index={}"), request.index),
                    request.runtime_token,
                    applied_world_context_object,
                    deps.runtime_state,
                    deps.targeted_runtime_ability_query_probe_enabled,
                    deps.runtime_context_summary,
                    deps.targeted_ability_query_functions,
                    deps.targeted_ability_probe_jsonl_emitter);
        }
        if (deps.runtime_ability_state_probe_enabled)
        {
            if (deps.ability_snapshot_emitter)
            {
                deps.ability_snapshot_emitter(
                        std::format(STR("post_runtime_grant:index={}:{}"), request.index, request.runtime_token),
                        applied_world_context_object);
            }
        }
        if (deps.live_ability_owner_probe_emitter)
        {
            deps.live_ability_owner_probe_emitter(
                    std::format(STR("post_runtime_grant:index={}:{}"), request.index, request.runtime_token),
                    applied_world_context_object);
        }
    }

    auto process_pending_runtime_item_grant(const RuntimeItemGrantPollDeps& deps) -> void
    {
        const RuntimeHotPathProfileScope poll_profile{STR("runtime_item_grant:poll")};
        const auto now = std::chrono::steady_clock::now();
        auto process_lock = [&]() {
            const RuntimeHotPathProfileScope profile{STR("runtime_item_grant:process_lock")};
            return deps.runtime_state.grant_state().item_grant_poll_state().try_acquire_process_lock();
        }();
        if (!process_lock.owns_lock())
        {
            if (deps.runtime_state.grant_state().log_throttle_state().mark_runtime_item_grant_log(now, item_grant_wait_log_interval))
            {
                Output::send<LogLevel::Verbose>(
                        STR("[KH3APProbeCpp] Runtime item grant process skipped reason=process_lock_busy context={}\n"),
                        std::wstring{deps.runtime_context_summary});
            }
            return;
        }

        // Read bound requests even during loading and excluded maps. The
        // delivery context gates below retain unsafe requests, and publish
        // their native waiting reason without trusting Python snapshots.
        const auto normal_request_path = [&]() {
            const RuntimeHotPathProfileScope profile{STR("runtime_item_grant:request_path")};
            return get_runtime_item_grant_path(deps.bridge_dir, deps.runtime_item_grant_filename);
        }();
        const auto priority_request_path = [&]() {
            const RuntimeHotPathProfileScope profile{STR("runtime_item_grant:priority_request_path")};
            return get_runtime_item_grant_path(deps.bridge_dir, deps.runtime_item_grant_priority_filename);
        }();
        const auto request_file_exists = [](const std::optional<std::filesystem::path>& path) {
            return path.has_value() && path_points_to_regular_file(*path);
        };
        auto& poll_state = deps.runtime_state.grant_state().item_grant_poll_state();
        const auto native = reconciliation::view();
        const bool scheduled_exists = native->delivery != nullptr || !poll_state.scheduled_queue().empty();
        const bool priority_request_exists = request_file_exists(priority_request_path) || !poll_state.delivery_queue(true).empty();
        // A pending Sora DeathLink must not starve inventory delivery in space.
        // Keep its file untouched for the normal Sora pipeline after landing.
        const bool space_active = RuntimeSpaceService::is_space_active();
        const bool using_priority_request = runtime_item_delivery_uses_priority(
                priority_request_exists, space_active,
                priority_request_exists && space_active && (request_file_exists(normal_request_path) || !poll_state.delivery_queue().empty() || scheduled_exists));
        const bool using_schedule = !using_priority_request && !request_file_exists(normal_request_path) && poll_state.delivery_queue().empty();
        const auto request_path = using_schedule ? std::optional<std::filesystem::path>{} :
            using_priority_request ? priority_request_path : normal_request_path;
        if (!deps.runtime_state.grant_state().item_grant_poll_state().is_armed())
        {
            const bool selected_request_file_exists = [&]() {
                const RuntimeHotPathProfileScope profile{STR("runtime_item_grant:initial_request_exists")};
                return request_file_exists(request_path);
            }();
            if (!selected_request_file_exists && (using_schedule ? !scheduled_exists : poll_state.delivery_queue(using_priority_request).empty()))
            {
                return;
            }

            deps.runtime_state.grant_state().item_grant_poll_state().arm(now, false);
            if (deps.runtime_state.grant_state().log_throttle_state().mark_runtime_item_grant_poll_log(now, item_grant_poll_log_interval))
            {
                Output::send<LogLevel::Verbose>(
                        STR("[KH3APProbeCpp] Runtime item poll self-armed pending request lane={} | context={}\n"),
                        using_priority_request ? std::wstring_view{STR("priority")} : std::wstring_view{STR("normal")},
                        std::wstring{deps.runtime_context_summary});
            }
        }

        if (!deps.runtime_state.grant_state().item_grant_poll_state().ready_to_process(deps.runtime_item_grant_delay, now))
        {
            log_runtime_item_grant_wait_if_changed(
                    deps,
                    now,
                    STR("<armed>"),
                    STR("armed_delay"),
                    STR("runtime_item_grant_delay"),
                    std::format(
                            STR("delay_ms={}"),
                            static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                                          deps.runtime_item_grant_delay)
                                                          .count())),
                    0);
            return;
        }
        const auto armed_wait = deps.runtime_state.grant_state().item_grant_poll_state().armed_wait(now);
        if (armed_wait >= deps.slow_wait_log_delay &&
            deps.runtime_state.grant_state().log_throttle_state().mark_runtime_item_grant_armed_wait_log(now, deps.slow_wait_log_delay))
        {
            Output::send<LogLevel::Warning>(
                    STR("[KH3APProbeCpp] Runtime item grant armed wait exceeded threshold waited_ms={} on_update_calls={} | context={}\n"),
                    static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(armed_wait).count()),
                    deps.runtime_state.ui_diagnostic_state().heartbeat_state().calls(),
                    std::wstring{deps.runtime_context_summary});
        }

        auto& queue = using_schedule ? poll_state.scheduled_queue() : poll_state.delivery_queue(using_priority_request);
        const auto prepared = using_schedule ? std::pair{native->delivery_revision, native->delivery} :
            request_path ? pipe::grant_batch(*request_path) : decltype(pipe::grant_batch({})){};
        if (prepared.first != queue.mailbox_revision)
        {
            const RuntimeHotPathProfileScope profile{STR("runtime_item_grant:batch_read")};
            const auto& incoming = prepared.second;
            if (!incoming)
            {
                // An authoritative DELETE or replacement by a single/
                // malformed command must not keep executing the old batch.
                queue.clear();
                queue.mailbox_revision = prepared.first;
            }
            else
            {
                const auto expanded = incoming->expand_catalog ?
                    (native->expanded ? item_delivery::expand_command(*incoming, *native->expanded) : std::nullopt) :
                    std::optional<RuntimeItemGrantBatchRequest>{*incoming};
                if (!expanded)
                {
                    publish_delivery_readiness(deps, incoming->request_id, L"waiting", L"awaiting_receipt_catalog",
                        L"The DLL has not received all requested receipt templates.", incoming->grants.size());
                    return;
                }
                if (!queue.replace(*expanded))
                {
                    publish_delivery_readiness(deps, incoming->request_id, L"rejected", L"queue_capacity",
                            std::format(L"Submit 1 through {} actions in contiguous receipt groups of at most {}.",
                                item_delivery::limits::request_actions, item_delivery::limits::receipt_actions), incoming->grants.size());
                    return;
                }
                queue.mailbox_revision = prepared.first;
            }
        }
        if (!queue.empty())
        {
            const bool persistent_context = !space_active && item_delivery::requires_persistent_delivery_context();
            const auto batch = queue.select(space_active || persistent_context, [space_active](auto grants) {
                const auto policy = runtime_item_delivery_policy(grants, L"", true);
                return space_active ? policy.supports_space_inventory :
                    policy.supports_kh02_persistent_rewards || policy.supports_alternate_bad_status ||
                    policy.supports_alternate_game_over;
            });
            if (!batch.grants.empty()) process_batch_delivery(deps, batch, request_path, now, &queue);
            else log_runtime_item_grant_wait_if_changed(deps, now, batch.request_id, L"batch",
                space_active ? L"space_items_waiting_for_sora" : L"persistent_reward_items_waiting_for_sora",
                L"No whole receipt group supports the current playable context.", 0);
            return;
        }
        // A completed typed batch can remain in the mailbox until its durable
        // AP ACK arrives. Never reinterpret it as a single grant.
        if (prepared.second) { poll_state.disarm(); return; }

        const auto request = [&]() {
            const RuntimeHotPathProfileScope profile{STR("runtime_item_grant:single_read")};
            return read_runtime_item_grant_request(request_path);
        }();
        if (!request.has_value())
        {
            deps.runtime_state.grant_state().item_grant_poll_state().disarm();
            deps.runtime_state.grant_state().item_grant_poll_state().clear_last_key_if_stale(now, item_grant_stale_key_age);
            return;
        }
        process_single_delivery(deps, *request, request_path, now);
    }
}
