#include "kh3ap/app/config.h"
#include "kh3ap/features/player/characters/character_swap.h"
#include "kh3ap/bridge/transport/bridge_channels.h"
#include "kh3ap/bridge/rewards/reconciliation.h"
#include "kh3ap/bridge/session/deathlink.h"
#include "kh3ap/features/world/space/space_service.h"
#include "kh3ap/features/grants/delivery/item_delivery_internal.h"
#include "kh3ap/bridge/transport/bridge_io_helpers.h"
#include "kh3ap/features/grants/grant_state.h"
#include "kh3ap/features/grants/grant_tokens.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/app/state.h"
#include "kh3ap/features/grants/vbonus/vbonus_grant_call_helpers.h"
#include "kh3ap/bridge/utilities.h"
#include "kh3ap/bridge/serialization/json_fields.h"
#include "kh3ap/features/grants/delivery/item_delivery_binding.h"
#define NOMINMAX
#include "kh3ap/features/kh02/story/kh02_story.h"

#include <Windows.h>

#include <DynamicOutput/DynamicOutput.hpp>
#include <UnrealDef.hpp>
#include <algorithm>
#include <chrono>
#include <format>
#include <utility>

namespace kh3ap::item_delivery
{
    using ::kh3ap::settings::s_enable_runtime_item_grant_apply;

    namespace
    {
        constexpr auto item_delivery_log_interval = std::chrono::seconds(5);
        constexpr auto slow_item_grant_threshold_ms = 100;
    }

    using namespace RC;
    using namespace RC::Unreal;

    auto process_batch_delivery(
            const RuntimeItemGrantPollDeps& deps,
            const RuntimeItemGrantBatchRequest& batch_request,
            const std::optional<std::filesystem::path>& request_path,
            std::chrono::steady_clock::time_point now,
            DeliveryQueue* queue) -> void
    {
        auto& poll_state = deps.runtime_state.grant_state().item_grant_poll_state();
        const auto consume_request = [&] {
            if (request_path.has_value())
            {
                try_remove_file(*request_path);
            }
        };
        poll_state.mark_seen(now);
        const std::wstring request_key = RuntimeGrantTokens::make_batch_request_key(batch_request);
        // A client may restart while the old process is still polling, or a
        // durable request may survive a game restart. Reject either before
        // inspecting or recovering any gameplay objects.
        const auto status = deps.bridge_dir ? read_text_file(*deps.bridge_dir / channels::ap_status) : std::nullopt;
        const auto slot = installed_delivery_seed_identity();
        const auto binding_error = delivery_binding_error(
                batch_request.request_schema, batch_request.process_id, GetCurrentProcessId(),
                batch_request.seed_name, batch_request.slot,
                status ? JsonFieldReader::string_field(*status, "seed_name").value_or(L"") : L"",
                status ? JsonFieldReader::int64_field(*status, "slot").value_or(0) : 0,
                slot.seed, slot.slot);
        const auto reconciliation_state = reconciliation::view();
        const auto epoch_error = batch_request.delivery_epoch != reconciliation::epoch() ||
            batch_request.delivery_epoch != reconciliation_state->epoch;
        if (epoch_error || !reconciliation_state->ready)
        {
            if (queue) queue->clear();
            publish_delivery_readiness(deps, batch_request.request_id, L"rejected",
                epoch_error ? L"request_delivery_epoch_mismatch" : L"native_reconciliation_not_ready",
                L"Wait for the native checkpoint and refresh the submission.", batch_request.grants.size());
            retire_runtime_item_delivery_request(poll_state, consume_request);
            return;
        }
        if (!binding_error.empty())
        {
            if (!request_path)
            {
                // Automatic receipts remain queued while the client binds the
                // installed seed. No new Python submission will replace them.
                publish_delivery_readiness(deps, batch_request.request_id, L"waiting",
                    binding_error, L"Waiting for the client and installed seed to match.", batch_request.grants.size());
                return;
            }
            if (queue) queue->clear();
            publish_delivery_readiness(deps, batch_request.request_id, L"rejected",
                    binding_error, L"No grants applied; submit for the current process and seed.",
                    batch_request.grants.size());
            retire_runtime_item_delivery_request(poll_state, consume_request);
            return;
        }
        if (poll_state.apply_in_progress())
        {
            log_runtime_item_grant_wait_if_changed(
                    deps,
                    now,
                    batch_request.request_id,
                    STR("batch"),
                    STR("apply_in_progress"),
                    poll_state.apply_in_progress_key(),
                    static_cast<uint64_t>(batch_request.grants.size()));
            if (deps.runtime_state.grant_state().log_throttle_state().mark_runtime_item_grant_log(now, item_delivery_log_interval))
            {
                Output::send<LogLevel::Verbose>(
                        STR("[KH3APProbeCpp] Runtime item poll skipped reentrant batch request_id={} active_key={} request_key={}\n"),
                        batch_request.request_id,
                        poll_state.apply_in_progress_key(),
                        request_key);
            }
            return;
        }
        if (!queue && poll_state.is_duplicate(request_key))
        {
            if (deps.runtime_state.grant_state().log_throttle_state().mark_runtime_item_grant_log(now, item_delivery_log_interval))
            {
                Output::send<LogLevel::Verbose>(
                        STR("[KH3APProbeCpp] Runtime item poll skipped duplicate batch grants={} request_id={}\n"),
                        static_cast<uint64_t>(batch_request.grants.size()),
                        batch_request.request_id);
            }
            retire_runtime_item_delivery_request(poll_state, consume_request);
            return;
        }

        const auto policy = runtime_item_delivery_policy(batch_request.grants, batch_request.action_kind, true);
        const bool allow_special_battle_game_over = policy.allow_special_battle_game_over;
        const auto death = deathlink::view();
        if (death->fenced && !allow_special_battle_game_over && !RuntimeSpaceService::is_space_active())
        {
            publish_delivery_readiness(deps, batch_request.request_id, L"waiting", L"deathlink_transition_pending",
                L"Native DeathLink owns Game Over and playable-pawn recovery.", batch_request.grants.size());
            return;
        }
        auto context = prepare_delivery_context(deps, policy, true, now);
        const auto& safety_status = context.safety;
        auto& world_context_candidates = context.candidates;
        const DeliveryTrace trace{
                deps, true, batch_request.request_id, batch_request.action_kind,
                batch_request.grants.size(), world_context_candidates};
        if (!safety_status.ready)
        {
            log_runtime_item_grant_wait_if_changed(
                    deps,
                    now,
                    batch_request.request_id,
                    STR("batch"),
                    safety_status.reason,
                    safety_status.details,
                    static_cast<uint64_t>(batch_request.grants.size()));
            const bool should_log_pending =
                    deps.runtime_state.grant_state().log_throttle_state().mark_manual_runtime_item_grant_log(now, item_delivery_log_interval);
            if (allow_special_battle_game_over && should_log_pending)
            {
                trace.deathlink(
                        STR("batch_wait"), nullptr,
                        runtime_item_grant_wait_ms(deps, now),
                        STR("waiting"),
                        safety_status.reason,
                        safety_status.details,
                        allow_special_battle_game_over);
            }
            if (should_log_pending)
            {
                Output::send<LogLevel::Verbose>(
                        STR("[KH3APProbeCpp] Runtime item pending batch grants={} reason={} details={} | context={}\n"),
                        static_cast<uint64_t>(batch_request.grants.size()),
                        safety_status.reason,
                        safety_status.details,
                        std::wstring{deps.runtime_context_summary});
            }
            return;
        }
        if constexpr (!s_enable_runtime_item_grant_apply)
        {
            log_runtime_item_grant_wait_if_changed(
                    deps,
                    now,
                    batch_request.request_id,
                    STR("batch"),
                    STR("apply_disabled"),
                    STR("runtime item grant apply disabled by isolation build"),
                    static_cast<uint64_t>(batch_request.grants.size()));
            return;
        }
        if (world_context_candidates.empty())
        {
            world_context_candidates = collect_delivery_world_candidates(deps, true);
        }
        if (world_context_candidates.empty())
        {
            log_runtime_item_grant_wait_if_changed(
                    deps,
                    now,
                    batch_request.request_id,
                    STR("batch"),
                    STR("no_world_context"),
                    STR("collect_fast_runtime_item_grant_world_context_candidates returned empty"),
                    static_cast<uint64_t>(batch_request.grants.size()));
            if (deps.runtime_state.grant_state().log_throttle_state().mark_manual_runtime_item_grant_log(now, item_delivery_log_interval))
            {
                Output::send<LogLevel::Verbose>(
                        STR("[KH3APProbeCpp] Runtime item pending batch grants={} reason=no_world_context\n"),
                        static_cast<uint64_t>(batch_request.grants.size()));
            }
            return;
        }

        RuntimeItemApplyBatchPayload batch_payload{
                .request_id = batch_request.request_id,
                .action_kind = batch_request.action_kind,
                .status = std::wstring{STR("applied")},
                .error = std::wstring{},
                .applied = {},
        };
        std::wstring failure_details{};
        batch_payload.process_id = batch_request.process_id;
        batch_payload.delivery_epoch = batch_request.delivery_epoch;
        batch_payload.seed_name = batch_request.seed_name;
        batch_payload.slot = batch_request.slot;
        const RuntimeItemDeliveryApplyScope apply_scope{poll_state, request_key};
        const auto batch_apply_started_at = std::chrono::steady_clock::now();
        batch_payload.runtime_request_wait_ms = static_cast<int64_t>(
                runtime_item_grant_wait_ms(deps, now));
        trace.profile(
                STR("runtime_batch_apply_begin"), nullptr,
                runtime_item_grant_wait_ms(deps, now),
                STR("begin"),
                STR(""));
        if (allow_special_battle_game_over)
        {
            trace.deathlink(
                    STR("batch_apply_begin"), nullptr,
                    runtime_item_grant_wait_ms(deps, now),
                    STR("begin"),
                    STR("apply_begin"),
                    STR(""),
                    allow_special_battle_game_over);
        }
        Output::send<LogLevel::Verbose>(
                STR("[KH3APProbeCpp] Runtime item grant apply begin request_id={} grants={} candidates={} waited_ms={} context={}\n"),
                batch_request.request_id,
                static_cast<uint64_t>(batch_request.grants.size()),
                static_cast<uint64_t>(world_context_candidates.size()),
                runtime_item_grant_wait_ms(deps, now),
                std::wstring{deps.runtime_context_summary});

        bool unsafe_batch_abort = false;
        bool budget_exhausted = false;
        const bool bundle_visible_durable_ability_hud = policy.bundle_visible_durable_ability_hud;
        std::vector<std::wstring> bundled_durable_ability_hud_tokens{};
        UObject* bundled_durable_ability_hud_pawn = nullptr;
        {
            const RuntimeHotPathProfileScope batch_grant_loop_profile{STR("runtime_item_grant:batch_grant_loop")};
            auto result = execute_runtime_item_delivery_batch(
                    batch_request, deps.bridge_dir, poll_state,
                    RuntimeItemDeliveryBatchCallbacks{
                            .apply = [&](const RuntimeItemGrantRequest& grant) {
                                if (grant.index >= 0 && grant.action_kind != L"ap_replay_batch" &&
                                    reconciliation_state->active.contains(grant.index))
                                    return RuntimeItemDeliveryAttempt{.applied = true, .duplicate = true,
                                        .payload = RuntimeGrantTokens::build_apply_payload(grant)};
                                if (queue)
                                    if (const auto cached = queue->cached(grant))
                                        return RuntimeItemDeliveryAttempt{.applied = true, .duplicate = true, .payload = *cached};
                                RuntimeItemDeliveryAttempt attempt{};
                                if (deathlink::view()->fenced && !allow_special_battle_game_over && !RuntimeSpaceService::is_space_active())
                                {
                                    attempt.unsafe_abort = true;
                                    attempt.failure_details = L"deathlink_transition_pending";
                                    return attempt;
                                }
                                auto& apply_payload = attempt.payload;
                                auto& applied_world_context_object = attempt.world_context;
                                const auto grant_apply_started_at = std::chrono::steady_clock::now();
                                auto& unsafe_abort_requested = attempt.unsafe_abort;
                                const bool grant_applied = [&]() {
                                    const RuntimeHotPathProfileScope profile{STR("runtime_item_grant:batch_grant_apply")};
                                    return try_apply_runtime_item_grant_request(
                                            grant,
                                            world_context_candidates,
                                            deps.apply_deps,
                                            &apply_payload,
                                            &failure_details,
                                            &applied_world_context_object,
                                            &unsafe_abort_requested);
                                }();
                                const uint64_t grant_elapsed_ms = elapsed_ms_since(grant_apply_started_at);
                                trace.profile(
                                        STR("runtime_batch_grant_apply_result"), &grant,
                                        grant_elapsed_ms,
                                        delivery_attempt_status(grant_applied, unsafe_abort_requested),
                                        grant_applied ? std::wstring_view{STR("")} : std::wstring_view{failure_details});
                                if (grant.grant_method == STR("bp_tres_start_game_over"))
                                {
                                    trace.deathlink(
                                            STR("batch_grant_apply_result"), &grant,
                                            runtime_item_grant_wait_ms(deps, now),
                                            delivery_attempt_status(grant_applied, unsafe_abort_requested),
                                            grant_applied ? std::wstring_view{STR("bp_tres_start_game_over_called")} : std::wstring_view{STR("grant_failed")},
                                            grant_applied ? std::wstring_view{STR("")} : std::wstring_view{failure_details},
                                            true);
                                }
                                if (grant_elapsed_ms >= slow_item_grant_threshold_ms)
                                {
                                    Output::send<LogLevel::Warning>(
                                            STR("[KH3APProbeCpp] Runtime batch grant slow index={} item={} token={} method={} elapsed_ms={} request_id={}\n"),
                                            grant.index,
                                            grant.item_name,
                                            grant.runtime_token,
                                            grant.grant_method,
                                            grant_elapsed_ms,
                                            grant.request_id.empty() ? batch_request.request_id : grant.request_id);
                                }
                                attempt.applied = grant_applied;
                                attempt.failure_details = failure_details;
                                return attempt;
                            },
                            .duplicate = [&](const RuntimeItemGrantRequest& grant) {
                                if (queue && !queue->cached(grant)) queue->remember(grant, RuntimeGrantTokens::build_apply_payload(grant));
                                if (queue) return; // Cached frame progress needs no repeated log work.
                                Output::send<LogLevel::Verbose>(
                                        STR("[KH3APProbeCpp] Runtime batch grant skipped duplicate fresh AP delivery index={} item={} token={} request_id={}\n"),
                                        grant.index,
                                        grant.item_name,
                                        grant.runtime_token,
                                        grant.request_id);
                            },
                            .applied = [&](const RuntimeItemGrantRequest& grant, const RuntimeItemDeliveryAttempt& attempt) {
                                if (queue) queue->remember(grant, attempt.payload);
                                UObject* const applied_world_context_object = attempt.world_context;
                                {
                                    const RuntimeHotPathProfileScope observer_profile{STR("runtime_item_grant:batch_observe_applied")};
                                    observe_runtime_item_grant_applied(
                                            grant,
                                            applied_world_context_object,
                                            true,
                                            deps.post_apply_observer_deps);
                                }
                                Output::send<LogLevel::Verbose>(
                                        STR("[KH3APProbeCpp] Applied runtime AP batch item index={} item={} token={} method={} from={} location={} | world={}\n"),
                                        grant.index,
                                        grant.item_name,
                                        grant.runtime_token,
                                        grant.grant_method,
                                        grant.from_player_name,
                                        grant.location_name,
                                        format_runtime_item_grant_world_for_log(deps, applied_world_context_object));
                                if (bundle_visible_durable_ability_hud)
                                {
                                    bundled_durable_ability_hud_tokens.push_back(grant.runtime_token);
                                    if (!bundled_durable_ability_hud_pawn)
                                    {
                                        bundled_durable_ability_hud_pawn = applied_world_context_object;
                                    }
                                }
                            },
                            .failed = [&](const RuntimeItemGrantRequest& grant, const RuntimeItemDeliveryAttempt& attempt,
                                          const RuntimeItemApplyBatchPayload& progress) {
                                if (attempt.unsafe_abort)
                                {
                                    Output::send<LogLevel::Warning>(
                                            STR("[KH3APProbeCpp] Runtime batch grant unsafe abort index={} item={} token={} method={} details={} applied_so_far={} requested={} request_id={}\n"),
                                            grant.index,
                                            grant.item_name,
                                            grant.runtime_token,
                                            grant.grant_method,
                                            progress.error,
                                            static_cast<uint64_t>(progress.applied.size()),
                                            static_cast<uint64_t>(batch_request.grants.size()),
                                            grant.request_id.empty() ? batch_request.request_id : grant.request_id);
                                    return;
                                }
                                Output::send<LogLevel::Warning>(
                                        STR("[KH3APProbeCpp] Runtime batch grant failed index={} item={} token={} method={} details={}\n"),
                                        grant.index,
                                        grant.item_name,
                                        grant.runtime_token,
                                        grant.grant_method,
                                        progress.error);
                            },
                    });
            batch_payload.status = std::move(result.payload.status);
            batch_payload.error = std::move(result.payload.error);
            batch_payload.applied = std::move(result.payload.applied);
            unsafe_batch_abort = result.unsafe_abort;
            budget_exhausted = result.budget_exhausted && result.new_progress;
        }

        if (queue)
        {
            batch_payload.applied = queue->complete_receipts(batch_request);
            batch_payload.request_complete = queue->completes_request(batch_payload.applied);
        }

        if (bundle_visible_durable_ability_hud && !persistent_player_reward_context() &&
            !bundled_durable_ability_hud_tokens.empty())
        {
            // The native presenter accepts a larger array, but the stock
            // VBonus widget renders at most two text rows per popup.
            constexpr size_t max_hud_lines = 2;
            for (size_t offset = 0;
                 offset < bundled_durable_ability_hud_tokens.size();
                 offset += max_hud_lines)
            {
                const size_t count = std::min(
                        max_hud_lines,
                        bundled_durable_ability_hud_tokens.size() - offset);
                const std::vector<std::wstring> hud_tokens{
                        bundled_durable_ability_hud_tokens.begin() +
                                static_cast<std::ptrdiff_t>(offset),
                        bundled_durable_ability_hud_tokens.begin() +
                                static_cast<std::ptrdiff_t>(offset + count),
                };
                std::wstring hud_failure{};
                const bool hud_presented =
                        try_present_native_vbonus_hud_ability_tokens(
                                hud_tokens,
                                bundled_durable_ability_hud_pawn,
                                deps.runtime_state,
                                &hud_failure);
                Output::send<LogLevel::Verbose>(
                        STR("[KH3APProbeCpp] Runtime durable ability HUD batch status={} offset={} count={} requested={} request_id={} error={}\n"),
                        hud_presented ? STR("presented") : STR("failed"),
                        offset,
                        count,
                        bundled_durable_ability_hud_tokens.size(),
                        batch_request.request_id,
                        hud_failure.empty()
                                ? std::wstring{STR("<none>")}
                                : hud_failure);
            }
        }

        batch_payload.runtime_grant_apply_elapsed_ms = static_cast<int64_t>(
                elapsed_ms_since(batch_apply_started_at));
        const auto batch_write_started_at = std::chrono::steady_clock::now();
        const bool batch_apply_event_written = [&]() {
            const RuntimeHotPathProfileScope profile{STR("runtime_item_grant:batch_apply_event_write")};
            // Commit every completed receipt group on the native worker. An
            // unfinished budgeted batch may be replaced by a newer snapshot.
            return !batch_payload.applied.empty() &&
                   write_runtime_item_apply_batch_event(
                           deps.bridge_dir,
                           deps.runtime_item_apply_filename,
                           batch_payload,
                           deps.protocol_name,
                           deps.protocol_version);
        }();
        trace.profile(
                STR("runtime_batch_apply_event_write"), nullptr,
                elapsed_ms_since(batch_write_started_at),
                batch_apply_event_written ? std::wstring_view{STR("written")} : std::wstring_view{STR("not_written")},
                batch_payload.error);
        const std::wstring batch_apply_event_path = deps.bridge_dir.has_value()
                ? (*deps.bridge_dir / std::filesystem::path{deps.runtime_item_apply_filename}).wstring()
                : std::wstring{STR("<no-bridge>")};
        const std::wstring batch_apply_indices = format_runtime_apply_indices(batch_payload);
        Output::send<LogLevel::Verbose>(
                STR("[KH3APProbeCpp] Runtime batch apply event write status={} path={} request_id={} applied_indices={} applied={} requested={} error={}\n"),
                batch_apply_event_written ? std::wstring_view{STR("written")} : std::wstring_view{STR("not_written")},
                batch_apply_event_path,
                batch_request.request_id,
                batch_apply_indices.empty() ? std::wstring_view{STR("<none>")} : std::wstring_view{batch_apply_indices},
                static_cast<uint64_t>(batch_payload.applied.size()),
                static_cast<uint64_t>(batch_request.grants.size()),
                batch_payload.error.empty() ? std::wstring_view{STR("<none>")} : std::wstring_view{batch_payload.error});
        if (allow_special_battle_game_over)
        {
            trace.deathlink(
                    STR("batch_apply_event_write"), nullptr,
                    runtime_item_grant_wait_ms(deps, now),
                    batch_apply_event_written ? std::wstring_view{STR("written")} : std::wstring_view{STR("not_written")},
                    batch_apply_event_written ? std::wstring_view{STR("apply_event_written")} : std::wstring_view{STR("apply_event_not_written")},
                    batch_payload.error.empty()
                    ? std::format(STR("applied_count={} requested_count={}"),
                    static_cast<uint64_t>(batch_payload.applied.size()),
                    static_cast<uint64_t>(batch_request.grants.size()))
                    : batch_payload.error,
                    allow_special_battle_game_over);
        }
        if (batch_apply_event_written && queue)
        {
            queue->acknowledge(batch_payload.applied);
            // Native accounting commits before emitting the client event.
            if (queue->empty()) poll_state.disarm();
            else poll_state.arm(now);
        }
        if (batch_apply_event_written && queue || (!queue && acknowledge_runtime_item_delivery(
                batch_apply_event_written, poll_state, request_key, now, consume_request)))
        {
            publish_delivery_readiness(deps, batch_request.request_id, batch_payload.status,
                    L"apply_acknowledged", batch_payload.error, batch_request.grants.size());
            Output::send<LogLevel::Verbose>(
                    STR("[KH3APProbeCpp] Applied runtime AP batch applied={} requested={} request_id={}\n"),
                    static_cast<uint64_t>(batch_payload.applied.size()),
                    static_cast<uint64_t>(batch_request.grants.size()),
                    batch_request.request_id);
        }
        else if (batch_payload.applied.empty())
        {
            log_runtime_item_grant_wait_if_changed(
                    deps,
                    now,
                    batch_request.request_id,
                    STR("batch"),
                    STR("apply_no_grants_applied"),
                    batch_payload.error.empty() ? std::wstring{STR("<none>")} : batch_payload.error,
                    static_cast<uint64_t>(batch_request.grants.size()));
            if (unsafe_batch_abort)
            {
                poll_state.disarm();
            }
        }
        trace.profile(
                STR("runtime_batch_apply_end"), nullptr,
                elapsed_ms_since(batch_apply_started_at),
                batch_payload.status,
                batch_payload.error);
        if (queue && budget_exhausted && !queue->empty()) poll_state.arm(now);
    }

    auto process_single_delivery(
            const RuntimeItemGrantPollDeps& deps,
            const RuntimeItemGrantRequest& request,
            const std::optional<std::filesystem::path>& request_path,
            std::chrono::steady_clock::time_point now) -> void
    {
        auto& poll_state = deps.runtime_state.grant_state().item_grant_poll_state();
        const auto consume_request = [&] {
            if (request_path.has_value())
            {
                try_remove_file(*request_path);
            }
        };
        poll_state.mark_seen(now);
        if (deps.runtime_state.grant_state().log_throttle_state().mark_runtime_item_grant_poll_log(now, item_delivery_log_interval))
        {
            Output::send<LogLevel::Verbose>(
                    STR("[KH3APProbeCpp] Runtime item poll pending armed=true index={} item={} token={} method={} flag={} request_id={} | context={}\n"),
                    request.index,
                    request.item_name,
                    request.runtime_token,
                    request.grant_method,
                    request.runtime_flag_name.empty() ? std::wstring{STR("<none>")} : request.runtime_flag_name,
                    request.request_id,
                    std::wstring{deps.runtime_context_summary});
        }

        const std::wstring request_key = RuntimeGrantTokens::make_request_key(request);
        if (poll_state.is_duplicate(request_key))
        {
            log_runtime_item_grant_wait_if_changed(
                    deps,
                    now,
                    request.request_id,
                    STR("single"),
                    STR("duplicate_request"),
                    request_key,
                    1);
            if (deps.runtime_state.grant_state().log_throttle_state().mark_runtime_item_grant_log(now, item_delivery_log_interval))
            {
                Output::send<LogLevel::Verbose>(
                        STR("[KH3APProbeCpp] Runtime item poll skipped duplicate index={} item={} token={} request_id={}\n"),
                        request.index,
                        request.item_name,
                        request.runtime_token,
                        request.request_id);
            }
            retire_runtime_item_delivery_request(poll_state, consume_request);
            return;
        }

        const auto policy = runtime_item_delivery_policy(
                std::span{&request, size_t{1}}, request.action_kind, false);
        const bool allow_special_battle_game_over = policy.allow_special_battle_game_over;
        if (deathlink::view()->fenced && !allow_special_battle_game_over && !RuntimeSpaceService::is_space_active())
        {
            publish_delivery_readiness(deps, request.request_id, L"waiting", L"deathlink_transition_pending",
                L"Native DeathLink owns Game Over and playable-pawn recovery.", 1);
            return;
        }
        auto context = prepare_delivery_context(deps, policy, false, now);
        const auto& safety_status = context.safety;
        auto& world_context_candidates = context.candidates;
        const DeliveryTrace trace{
                deps, false, request.request_id, request.action_kind, 1, world_context_candidates};
        if (!safety_status.ready)
        {
            log_runtime_item_grant_wait_if_changed(
                    deps,
                    now,
                    request.request_id,
                    STR("single"),
                    safety_status.reason,
                    safety_status.details,
                    1);
            const bool should_log_pending =
                    deps.runtime_state.grant_state().log_throttle_state().mark_manual_runtime_item_grant_log(now, item_delivery_log_interval);
            if (allow_special_battle_game_over && should_log_pending)
            {
                trace.deathlink(
                        STR("single_wait"), &request,
                        runtime_item_grant_wait_ms(deps, now),
                        STR("waiting"),
                        safety_status.reason,
                        safety_status.details,
                        allow_special_battle_game_over);
            }
            if (should_log_pending)
            {
                Output::send<LogLevel::Verbose>(
                        STR("[KH3APProbeCpp] Runtime item pending index={} item={} token={} reason={} details={} | context={}\n"),
                        request.index,
                        request.item_name,
                        request.runtime_token,
                        safety_status.reason,
                        safety_status.details,
                        std::wstring{deps.runtime_context_summary});
            }
            return;
        }
        if constexpr (!s_enable_runtime_item_grant_apply)
        {
            log_runtime_item_grant_wait_if_changed(
                    deps,
                    now,
                    request.request_id,
                    STR("single"),
                    STR("apply_disabled"),
                    STR("runtime item grant apply disabled by isolation build"),
                    1);
            return;
        }
        if (world_context_candidates.empty())
        {
            world_context_candidates = collect_delivery_world_candidates(deps, false);
        }
        if (world_context_candidates.empty())
        {
            log_runtime_item_grant_wait_if_changed(
                    deps,
                    now,
                    request.request_id,
                    STR("single"),
                    STR("no_world_context"),
                    STR("collect_fast_runtime_item_grant_world_context_candidates returned empty"),
                    1);
            if (deps.runtime_state.grant_state().log_throttle_state().mark_manual_runtime_item_grant_log(now, item_delivery_log_interval))
            {
                Output::send<LogLevel::Verbose>(
                        STR("[KH3APProbeCpp] Runtime item pending index={} item={} token={} reason=no_world_context\n"),
                        request.index,
                        request.item_name,
                        request.runtime_token);
            }
            return;
        }

        std::wstring failure_details{};
        const std::wstring bundle_summary = RuntimeGrantTokens::build_bundle_summary(
                request.bundle_display,
                request.randomized_summary,
                request.randomized_tokens,
                request.runtime_token);
        std::chrono::steady_clock::time_point single_apply_started_at{};
        auto attempt = execute_runtime_item_delivery_grant(
                request, request.action_kind, deps.bridge_dir, poll_state,
                [&](const RuntimeItemGrantRequest&) {
                    RuntimeItemDeliveryAttempt attempt{};
                    if (deathlink::view()->fenced && !allow_special_battle_game_over && !RuntimeSpaceService::is_space_active())
                    {
                        attempt.unsafe_abort = true;
                        attempt.failure_details = L"deathlink_transition_pending";
                        return attempt;
                    }
                    auto& payload = attempt.payload;
                    auto& applied_world_context_object = attempt.world_context;
                    Output::send<LogLevel::Verbose>(
                            STR("[KH3APProbeCpp] Runtime item grant apply begin request_id={} index={} item={} token={} method={} candidates={} waited_ms={} context={}\n"),
                            request.request_id,
                            request.index,
                            request.item_name,
                            request.runtime_token,
                            request.grant_method,
                            static_cast<uint64_t>(world_context_candidates.size()),
                            runtime_item_grant_wait_ms(deps, now),
                            std::wstring{deps.runtime_context_summary});
                    single_apply_started_at = std::chrono::steady_clock::now();
                    trace.profile(
                            STR("runtime_single_apply_begin"), &request,
                            runtime_item_grant_wait_ms(deps, now),
                            STR("begin"),
                            STR(""));
                    if (allow_special_battle_game_over)
                    {
                        trace.deathlink(
                                STR("single_apply_begin"), &request,
                                runtime_item_grant_wait_ms(deps, now),
                                STR("begin"),
                                STR("apply_begin"),
                                STR(""),
                                allow_special_battle_game_over);
                    }
                    const auto single_grant_started_at = std::chrono::steady_clock::now();
                    auto& unsafe_single_abort = attempt.unsafe_abort;
                    const bool single_grant_applied = [&]() {
                        const RuntimeHotPathProfileScope profile{STR("runtime_item_grant:single_grant_apply")};
                        return try_apply_runtime_item_grant_request(
                                request,
                                world_context_candidates,
                                deps.apply_deps,
                                &payload,
                                &failure_details,
                                &applied_world_context_object,
                                &unsafe_single_abort);
                    }();
                    const uint64_t single_grant_elapsed_ms = elapsed_ms_since(single_grant_started_at);
                    trace.profile(
                            STR("runtime_single_grant_apply_result"), &request,
                            single_grant_elapsed_ms,
                            delivery_attempt_status(single_grant_applied, unsafe_single_abort),
                            single_grant_applied ? std::wstring_view{STR("")} : std::wstring_view{failure_details});
                    if (allow_special_battle_game_over)
                    {
                        trace.deathlink(
                                STR("single_grant_apply_result"), &request,
                                runtime_item_grant_wait_ms(deps, now),
                                delivery_attempt_status(single_grant_applied, unsafe_single_abort),
                                single_grant_applied ? std::wstring_view{STR("bp_tres_start_game_over_called")} : std::wstring_view{STR("grant_failed")},
                                single_grant_applied ? std::wstring_view{STR("")} : std::wstring_view{failure_details},
                                allow_special_battle_game_over);
                    }
                    if (single_grant_elapsed_ms >= slow_item_grant_threshold_ms)
                    {
                        Output::send<LogLevel::Warning>(
                                STR("[KH3APProbeCpp] Runtime single grant slow index={} item={} token={} method={} elapsed_ms={} request_id={}\n"),
                                request.index,
                                request.item_name,
                                request.runtime_token,
                                request.grant_method,
                                single_grant_elapsed_ms,
                                request.request_id);
                    }
                    attempt.applied = single_grant_applied;
                    attempt.failure_details = failure_details;
                    return attempt;
                });
        auto& payload = attempt.payload;
        UObject* const applied_world_context_object = attempt.world_context;
        const bool unsafe_single_abort = attempt.unsafe_abort;
        if (attempt.duplicate)
        {
            RuntimeItemApplyPayload& duplicate_payload = attempt.payload;
            duplicate_payload.runtime_request_wait_ms = static_cast<int64_t>(
                    runtime_item_grant_wait_ms(deps, now));
            duplicate_payload.runtime_grant_apply_elapsed_ms = 0;
            const bool duplicate_apply_event_written = [&]() {
                const RuntimeHotPathProfileScope profile{STR("runtime_item_grant:single_duplicate_apply_event_write")};
                return write_runtime_item_apply_event(
                        deps.bridge_dir,
                        deps.runtime_item_apply_filename,
                        duplicate_payload,
                        deps.protocol_name,
                        deps.protocol_version);
            }();
            acknowledge_runtime_item_delivery(
                    duplicate_apply_event_written, poll_state, request_key, now, consume_request);
            Output::send<LogLevel::Verbose>(
                    STR("[KH3APProbeCpp] Runtime item grant skipped duplicate fresh AP delivery index={} item={} token={} request_id={}\n"),
                    request.index,
                    request.item_name,
                    request.runtime_token,
                    request.request_id);
            return;
        }
        if (attempt.applied)
        {
            payload.runtime_request_wait_ms = static_cast<int64_t>(
                    runtime_item_grant_wait_ms(deps, now));
            payload.runtime_grant_apply_elapsed_ms = static_cast<int64_t>(
                    elapsed_ms_since(single_apply_started_at));
            const auto single_write_started_at = std::chrono::steady_clock::now();
            const bool single_apply_event_written = [&]() {
                const RuntimeHotPathProfileScope profile{STR("runtime_item_grant:single_apply_event_write")};
                return write_runtime_item_apply_event(
                        deps.bridge_dir,
                        deps.runtime_item_apply_filename,
                        payload,
                        deps.protocol_name,
                        deps.protocol_version);
            }();
            trace.profile(
                    STR("runtime_single_apply_event_write"), &request,
                    elapsed_ms_since(single_write_started_at),
                    single_apply_event_written ? std::wstring_view{STR("written")} : std::wstring_view{STR("not_written")},
                    STR(""));
            if (allow_special_battle_game_over)
            {
                trace.deathlink(
                        STR("single_apply_event_write"), &request,
                        runtime_item_grant_wait_ms(deps, now),
                        single_apply_event_written ? std::wstring_view{STR("written")} : std::wstring_view{STR("not_written")},
                        single_apply_event_written ? std::wstring_view{STR("apply_event_written")} : std::wstring_view{STR("apply_event_not_written")},
                        STR(""),
                        allow_special_battle_game_over);
            }
            if (acknowledge_runtime_item_delivery(
                    single_apply_event_written, poll_state, request_key, now, consume_request))
            {
                {
                    const RuntimeHotPathProfileScope observer_profile{STR("runtime_item_grant:single_observe_applied")};
                    observe_runtime_item_grant_applied(
                            request,
                            applied_world_context_object,
                            false,
                            deps.post_apply_observer_deps);
                }
                Output::send<LogLevel::Verbose>(
                        STR("[KH3APProbeCpp] Applied runtime AP item index={} item={} token={} method={} bundle={} from={} location={} | world={}\n"),
                        request.index,
                        request.item_name,
                        request.runtime_token,
                        request.grant_method,
                        bundle_summary.empty() ? std::wstring{STR("<none>")} : bundle_summary,
                        request.from_player_name,
                        request.location_name,
                        format_runtime_item_grant_world_for_log(deps, applied_world_context_object));
            }
            trace.profile(
                    STR("runtime_single_apply_end"), &request,
                    elapsed_ms_since(single_apply_started_at),
                    single_apply_event_written ? std::wstring_view{STR("applied")} : std::wstring_view{STR("apply_event_write_failed")},
                    STR(""));
            return;
        }
        trace.profile(
                STR("runtime_single_apply_end"), &request,
                elapsed_ms_since(single_apply_started_at),
                STR("failed"),
                failure_details.empty() ? std::wstring{STR("grant_failed")} : failure_details);

        log_runtime_item_grant_wait_if_changed(
                deps,
                now,
                request.request_id,
                STR("single"),
                STR("apply_failed"),
                failure_details.empty() ? std::wstring{STR("grant_failed")} : failure_details,
                1);
        if (deps.runtime_state.grant_state().log_throttle_state().mark_runtime_item_grant_log(now, item_delivery_log_interval))
        {
            Output::send<LogLevel::Warning>(
                    STR("[KH3APProbeCpp] Runtime item grant call failed index={} item={} token={} method={} bundle={} details={} unsafe_abort={}\n"),
                    request.index,
                    request.item_name,
                    request.runtime_token,
                    request.grant_method,
                    bundle_summary.empty() ? std::wstring{STR("<none>")} : bundle_summary,
                    failure_details.empty() ? std::wstring{STR("grant_failed")} : failure_details,
                    unsafe_single_abort ? STR("true") : STR("false"));
        }
        if (unsafe_single_abort)
        {
            poll_state.disarm();
        }
    }
}
