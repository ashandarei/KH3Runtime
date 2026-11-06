#include "kh3ap/features/world/portals/portal_spawn_helpers.h"
#include "kh3ap/features/world/portals/portal_spawn.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/bridge/utilities.h"
#include <UnrealDef.hpp>
#include <Unreal/UObject.hpp>
#include <string>
#include "kh3ap/features/world/portals/portal_auto_spawn_state.h"
#include "kh3ap/features/world/portals/portal_result_writer.h"
#include "kh3ap/features/player/context/context_helpers.h"
#include "kh3ap/features/grants/grant_state.h"
#include "kh3ap/app/state.h"
#include "kh3ap/core/objects/uobject_weak.h"
#include <DynamicOutput/DynamicOutput.hpp>
#include <chrono>
#include <format>
#include <utility>

namespace kh3ap
{
    using namespace RC;
    using namespace RC::Unreal;
    using namespace portal_spawn_detail;
    namespace
    {
        auto make_auto_portal_request_id(std::wstring_view request_id_prefix,
                                         std::wstring_view opened_at) -> std::wstring
        {
            std::wstring request_id{request_id_prefix};
            if (!opened_at.empty())
            {
                request_id += STR("-");
                request_id += opened_at;
            }
            return request_id;
        }

        auto
        enqueue_auto_portal_event(const RuntimePortalSpawnDeps& deps, std::wstring_view source_name,
                                  std::wstring_view request_id_prefix, std::wstring_view opened_at,
                                  std::wstring_view stage, std::wstring_view status,
                                  std::wstring_view reason, std::wstring_view map_path,
                                  std::wstring_view context_key, std::wstring_view runtime_context,
                                  std::wstring_view pawn_name, UObject* pawn,
                                  UObject* spawned_actor, std::wstring_view class_resolution_source,
                                  uint64_t stable_ms, double x, double y, double z) -> void
        {
            if (!deps.bridge_dir.has_value())
            {
                return;
            }

            PortalSpawnResult result{};
            result.result_path =
                *deps.bridge_dir / std::filesystem::path{std::wstring{deps.result_filename}};
            result.events_path =
                *deps.bridge_dir / std::filesystem::path{std::wstring{deps.events_filename}};
            result.protocol_name = deps.protocol_name;
            result.protocol_version = deps.protocol_version;
            result.stage = stage;
            result.status = status;
            result.reason = reason;
            result.source_name = source_name;
            result.request_id = make_auto_portal_request_id(request_id_prefix, opened_at);
            result.build_stamp = deps.build_stamp;
            result.map_path = map_path;
            result.context_key = context_key;
            result.runtime_context = runtime_context;
            result.pawn_name = pawn_name;
            result.spawned_actor_name = spawned_actor && kh3ap::is_uobject_valid_fast(spawned_actor)
                                            ? format_object_debug_name(spawned_actor)
                                            : std::wstring{STR("<null>")};
            result.portal_class_path = deps.portal_class_path;
            result.portal_blueprint_path = deps.portal_blueprint_path;
            result.class_resolution_source = class_resolution_source;
            result.pawn_address = reinterpret_cast<uintptr_t>(pawn);
            result.spawned_actor_address = reinterpret_cast<uintptr_t>(spawned_actor);
            result.stable_ms = stable_ms;
            result.x = x;
            result.y = y;
            result.z = z;
            enqueue_portal_spawn_result(std::move(result));
        }
    } // namespace

    auto process_runtime_map_portal_auto_spawn(
        RuntimeAutoPortalSpawnState& state, const RuntimePortalSpawnDeps& deps,
        std::chrono::steady_clock::time_point now, std::wstring_view source_name,
        std::wstring_view request_id_prefix, std::wstring_view map_path, double x, double y,
        double z, std::chrono::steady_clock::duration stable_delay,
        std::chrono::steady_clock::duration retry_interval,
        std::chrono::steady_clock::duration verification_delay,
        std::wstring_view map_ready_context_key, bool spawn_eligible,
        std::wstring_view ineligibility_reason) -> void
    {
        if (!deps.bridge_dir.has_value())
        {
            return;
        }

        auto& context_state = deps.runtime_state.context_subsystem_state().context_state();
        const auto snapshot = context_state.current_snapshot();
        if (!snapshot.has_value())
        {
            return;
        }

        auto& pawn_state = deps.runtime_state.grant_state().pawn_state();
        UObject* const pawn = pawn_state.object();
        if (!pawn || !pawn_state.candidate_trusted() || pawn_state.name().empty())
        {
            return;
        }

        const std::wstring context_text =
            snapshot->world + STR(" ") + snapshot->outer + STR(" ") + snapshot->object;
        const RuntimePortalAutoSpawnContext spawn_context =
            resolve_runtime_portal_auto_spawn_context(
                snapshot->hook, snapshot->world, snapshot->outer, snapshot->object,
                context_state.last_playable_context_object(), pawn_state.name(), pawn, map_path,
                map_ready_context_key, state.last_playable_context_key);
        if (!spawn_context.target_map_is_current())
        {
            state.last_context_key.clear();
            state.last_playable_context_key.clear();
            state.reset_spawn_work();
            return;
        }
        if (!spawn_context.can_progress())
        {
            return;
        }

        const std::wstring& context_key = spawn_context.context_key;
        if (!spawn_context.used_pause_menu_fallback)
        {
            state.last_playable_context_key = context_key;
        }
        const bool spawn_readiness_confirmed =
            spawn_context.used_prior_playable_context || map_ready_context_key == context_key;
        const auto enqueue_lifecycle_event =
            [&](std::wstring_view stage, std::wstring_view status, std::wstring_view reason,
                std::wstring_view class_resolution_source, UObject* spawned_actor)
        {
            auto& event_pawn_state = deps.runtime_state.grant_state().pawn_state();
            UObject* const event_pawn = event_pawn_state.object();
            const uint64_t stable_ms =
                event_pawn
                    ? static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                                event_pawn_state.stable_for(now))
                                                .count())
                    : 0;
            enqueue_auto_portal_event(deps, source_name, request_id_prefix, snapshot->opened_at,
                                      stage, status, reason, map_path, context_key, context_text,
                                      snapshot->object, event_pawn, spawned_actor,
                                      class_resolution_source, stable_ms, x, y, z);
        };
        if (state.last_context_key != context_key)
        {
            state.last_context_key = context_key;
            state.reset_spawn_work();
        }

        const auto request_automatic_class_load = [&](std::wstring* resolution_source)
        {
            if (!runtime_portal_class_load_request_allowed(state.class_load_request_count))
            {
                append_resolution_detail(resolution_source,
                                         STR("portal_class_load_request_limit_reached"));
                state.class_load_failed_closed = true;
                return false;
            }

            ++state.class_load_request_count;
            return request_portal_class_load(deps, pawn, resolution_source);
        };

        if (!spawn_eligible)
        {
            const std::wstring effective_reason = ineligibility_reason.empty()
                                                      ? std::wstring{STR("spawn_condition_not_met")}
                                                      : std::wstring{ineligibility_reason};
            if (state.last_ineligibility_reason != effective_reason)
            {
                enqueue_lifecycle_event(STR("eligibility"), STR("ineligible"), effective_reason,
                                        STR(""), state.spawned_actor.Get());
                state.last_ineligibility_reason = effective_reason;
            }

            return;
        }
        if (!state.last_ineligibility_reason.empty())
        {
            enqueue_lifecycle_event(STR("eligibility"), STR("eligible"),
                                    STR("all_spawn_conditions_met"), STR(""), nullptr);
            state.last_ineligibility_reason.clear();
        }

        if (!state.verification_complete &&
            state.verification_not_before.time_since_epoch().count() != 0 &&
            now >= state.verification_not_before)
        {
            const bool portal_actor_is_live =
                kh3ap::is_uobject_valid_fast(state.spawned_actor.Get());
            if (portal_actor_is_live)
            {
                state.verification_complete = true;
                enqueue_lifecycle_event(STR("verification"), STR("verified"),
                                        state.verification_retry_attempted
                                            ? STR("portal_actor_present_after_retry")
                                            : STR("portal_actor_present"),
                                        STR(""), state.spawned_actor.Get());
                Output::send<LogLevel::Verbose>(
                    STR("[KH3APProbeCpp] RuntimeMapPortalVerify source={} status=present actor={} "
                        "retry_attempted={}\n"),
                    source_name, format_object_debug_name(state.spawned_actor.Get()),
                    state.verification_retry_attempted ? STR("true") : STR("false"));
            }
            else if (!state.verification_retry_armed && !state.verification_retry_attempted)
            {
                state.verification_retry_armed = true;
                state.spawn_attempted = false;
                state.playable_asset_load_requested = false;
                state.spawned_actor = RC::Unreal::FWeakObjectPtr{};
                state.spawned_actor_name.clear();
                state.last_attempt_at = {};
                state.verification_not_before = {};
                const std::wstring verification_reason =
                    std::format(STR("portal_actor_missing;retry_armed=true;map_ready_match={}"),
                                spawn_readiness_confirmed ? STR("true") : STR("false"));
                enqueue_lifecycle_event(STR("verification"), STR("retrying"), verification_reason,
                                        STR(""), nullptr);
                Output::send<LogLevel::Verbose>(
                    STR("[KH3APProbeCpp] RuntimeMapPortalVerify source={} status=missing "
                        "retry_armed=true map_ready_match={}\n"),
                    source_name, spawn_readiness_confirmed ? STR("true") : STR("false"));
            }
            else if (state.verification_retry_attempted && state.spawn_attempted)
            {
                state.verification_complete = true;
                enqueue_lifecycle_event(STR("verification"), STR("failed"),
                                        STR("portal_actor_missing_after_retry"), STR(""), nullptr);
                Output::send<LogLevel::Verbose>(
                    STR("[KH3APProbeCpp] RuntimeMapPortalVerify source={} "
                        "status=missing_after_retry retry_armed=false\n"),
                    source_name);
            }
        }

        if (state.spawn_attempted)
        {
            return;
        }

        const auto stable_for = pawn_state.stable_for(now);
        if (stable_delay.count() > 0 && stable_for < stable_delay)
        {
            return;
        }
        if (!state.asset_load_requested)
        {
            if (retry_interval.count() > 0 &&
                state.last_attempt_at.time_since_epoch().count() != 0 &&
                now - state.last_attempt_at < retry_interval)
            {
                return;
            }

            // Preserve the existing timing: start the background writer only in
            // the non-playable preload window, not on the first map-context tick.
            initialize_portal_result_writer();
            if (!state.context_entered_logged)
            {
                state.context_entered_logged = true;
                enqueue_lifecycle_event(
                    STR("context"), STR("observed"),
                    spawn_context.used_prior_playable_context
                        ? STR("target_map_pause_menu_context_with_prior_playable_pawn")
                        : (spawn_context.used_pause_menu_fallback
                               ? STR("target_map_pause_menu_context_with_cached_pawn")
                               : STR("target_map_context_detected_with_stable_pawn")),
                    STR(""), nullptr);
            }
            const auto preload_started_at = std::chrono::steady_clock::now();
            std::wstring preload_source{};
            Output::send<LogLevel::Verbose>(
                STR("[KH3APProbeCpp] RuntimeMapPortalPreload begin source={} context={} "
                    "stable_ms={}\n"),
                source_name, context_key,
                static_cast<uint64_t>(
                    std::chrono::duration_cast<std::chrono::milliseconds>(stable_for).count()));
            UClass* const ready_class = try_find_loaded_portal_class(deps, &preload_source);
            bool requested = false;
            if (!ready_class)
            {
                state.last_attempt_at = now;
                requested = request_automatic_class_load(&preload_source);
            }
            state.asset_load_requested = true;
            if (ready_class)
            {
                state.playable_asset_load_requested = true;
                state.class_load_failed_closed = false;
            }
            else if (state.class_load_failed_closed)
            {
                state.spawn_attempted = true;
                state.verification_complete = true;
            }
            const uint64_t preload_us =
                static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
                                          std::chrono::steady_clock::now() - preload_started_at)
                                          .count());
            Output::send<LogLevel::Verbose>(
                STR("[KH3APProbeCpp] RuntimeMapPortalPreload end source={} status={} detail={} "
                    "elapsed_us={}\n"),
                source_name,
                ready_class ? STR("retained")
                : requested ? STR("requested")
                            : STR("failed_closed"),
                preload_source.empty() ? std::wstring{STR("<none>")} : preload_source, preload_us);
            enqueue_lifecycle_event(STR("preload"),
                                    ready_class ? STR("ready")
                                    : requested ? STR("waiting")
                                                : STR("failed"),
                                    ready_class ? STR("portal_class_retained")
                                    : requested ? STR("asset_load_requested")
                                    : state.class_load_failed_closed
                                        ? STR("asset_load_request_limit_reached")
                                        : STR("asset_load_request_failed"),
                                    preload_source, nullptr);
            if (!ready_class)
            {
                return;
            }
        }
        if (!spawn_readiness_confirmed)
        {
            if (!state.map_ready_wait_logged)
            {
                state.map_ready_wait_logged = true;
                const std::wstring map_ready_reason = std::format(
                    STR("map_ready_context_mismatch;observed={}"),
                    map_ready_context_key.empty() ? std::wstring{STR("<empty>")}
                                                  : truncate_for_log(map_ready_context_key, 220));
                enqueue_lifecycle_event(STR("readiness"), STR("waiting"), map_ready_reason, STR(""),
                                        nullptr);
            }
            return;
        }
        if (retry_interval.count() > 0 && state.last_attempt_at.time_since_epoch().count() != 0 &&
            now - state.last_attempt_at < retry_interval)
        {
            return;
        }
        if (!state.playable_asset_load_requested)
        {
            const auto playable_load_started_at = std::chrono::steady_clock::now();
            std::wstring playable_load_source{};
            UClass* const ready_class = try_find_loaded_portal_class(deps, &playable_load_source);
            bool requested = false;
            if (!ready_class)
            {
                state.last_attempt_at = now;
                requested = request_automatic_class_load(&playable_load_source);
            }
            state.playable_asset_load_requested = true;
            if (ready_class)
            {
                state.class_load_failed_closed = false;
            }
            else if (state.class_load_failed_closed)
            {
                state.spawn_attempted = true;
                state.verification_complete = true;
            }
            const uint64_t playable_load_us = static_cast<uint64_t>(
                std::chrono::duration_cast<std::chrono::microseconds>(
                    std::chrono::steady_clock::now() - playable_load_started_at)
                    .count());
            Output::send<LogLevel::Verbose>(
                STR("[KH3APProbeCpp] RuntimeMapPortalPlayableLoad source={} status={} detail={} "
                    "elapsed_us={}\n"),
                source_name,
                ready_class ? STR("retained")
                : requested ? STR("requested")
                            : STR("failed_closed"),
                playable_load_source.empty() ? std::wstring{STR("<none>")} : playable_load_source,
                playable_load_us);
            enqueue_lifecycle_event(STR("playable_load"),
                                    ready_class ? STR("ready")
                                    : requested ? STR("waiting")
                                                : STR("failed"),
                                    ready_class ? STR("portal_class_retained")
                                    : requested ? STR("asset_load_requested")
                                    : state.class_load_failed_closed
                                        ? STR("asset_load_request_limit_reached")
                                        : STR("asset_load_request_failed"),
                                    playable_load_source, nullptr);
            if (!ready_class)
            {
                return;
            }
        }

        state.last_attempt_at = now;
        const PortalSpawnVector location{x, y, z};

        std::wstring class_resolution_source{};
        std::wstring failure_reason{};
        const bool verification_retry = state.verification_retry_armed;
        const auto spawn_started_at = std::chrono::steady_clock::now();
        UObject* const spawned_actor =
            spawn_portal(deps, pawn, location, false, &class_resolution_source, &failure_reason);

        const bool spawned = spawned_actor && kh3ap::is_uobject_valid_fast(spawned_actor);
        const bool class_reload_needed =
            !spawned &&
            class_resolution_source.find(STR("portal_class_not_loaded")) != std::wstring::npos;
        bool load_requested = false;
        bool class_reload_pending = false;
        if (class_reload_needed)
        {
            std::wstring reload_source{};
            load_requested = request_automatic_class_load(&reload_source);
            class_reload_pending = !state.class_load_failed_closed;
            append_resolution_detail(&class_resolution_source, reload_source);
            failure_reason = state.class_load_failed_closed
                                 ? std::wstring{STR("portal_class_reload_limit_reached")}
                             : load_requested
                                 ? std::wstring{STR("portal_class_reload_requested")}
                                 : std::wstring{STR("portal_class_reload_request_failed")};
        }
        if (spawned)
        {
            if (verification_retry)
            {
                state.verification_retry_armed = false;
                state.verification_retry_attempted = true;
            }
            state.spawn_attempted = true;
            state.spawned_actor = make_uobject_weak(spawned_actor);
            state.spawned_actor_name = std::wstring{deps.portal_class_path};
            state.verification_not_before = now + verification_delay;
            state.verification_complete = false;
        }
        else if (class_reload_pending)
        {
            state.spawn_attempted = false;
            state.playable_asset_load_requested = true;
            state.spawned_actor = RC::Unreal::FWeakObjectPtr{};
            state.spawned_actor_name.clear();
        }
        else
        {
            state.spawn_attempted = true;
            state.verification_complete = true;
            state.spawned_actor = RC::Unreal::FWeakObjectPtr{};
            state.spawned_actor_name.clear();
        }

        enqueue_lifecycle_event(STR("spawn"),
                                spawned                ? STR("spawned")
                                : class_reload_pending ? STR("waiting")
                                                       : STR("failed"),
                                spawned ? STR("") : failure_reason, class_resolution_source,
                                spawned_actor);

        const bool spawned_actor_still_real =
            spawned_actor && kh3ap::is_uobject_valid_fast(spawned_actor);
        if (spawned && !spawned_actor_still_real)
        {
            state.spawned_actor = RC::Unreal::FWeakObjectPtr{};
            state.spawned_actor_name.clear();
        }

        const uint64_t spawn_elapsed_us =
            static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
                                      std::chrono::steady_clock::now() - spawn_started_at)
                                      .count());
        Output::send<LogLevel::Verbose>(
            STR("[KH3APProbeCpp] RuntimeMapPortalAutoSpawn source={} status={} reason={} "
                "elapsed_us={} post_spawn_real={} verification_retry={} class_load_requests={}\n"),
            source_name,
            spawned ? STR("spawned") : (class_reload_pending ? STR("waiting") : STR("failed")),
            failure_reason.empty() ? std::wstring{STR("<none>")} : failure_reason, spawn_elapsed_us,
            spawned_actor_still_real ? STR("true") : STR("false"),
            verification_retry ? STR("true") : STR("false"), state.class_load_request_count);
    }
} // namespace kh3ap
