#include "kh3ap/features/story/save_points/internal.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/bridge/utilities.h"
#include "kh3ap/app/state.h"
#include "kh3ap/features/grants/grant_state.h"
#include "kh3ap/features/player/objects/object_resolver.h"
#include "kh3ap/core/objects/asset_load_helpers.h"
#include "kh3ap/bridge/transport/bridge_io_helpers.h"
#include <UnrealDef.hpp>
#include <Unreal/FProperty.hpp>
#include <Unreal/UClass.hpp>
#include <Unreal/UFunction.hpp>
#include <Unreal/UObject.hpp>
#include <Unreal/UObjectGlobals.hpp>
#include <DynamicOutput/DynamicOutput.hpp>
#include <array>
#include <chrono>
#include <filesystem>
#include <optional>
#include <string>

namespace kh3ap
{
    using namespace RC;
    using namespace RC::Unreal;

    namespace
    {
        using namespace save_point_detail;
        using save_point_text::auto_spawn_context_key;
        using save_point_text::runtime_context_matches_map;
        using save_point_text::sanitize_name_token;

    } // namespace

    auto spawn_runtime_player_start_for_route(UObject* world_context,
                                              std::wstring_view player_start_tag,
                                              std::wstring_view disp_map_name, double x, double y,
                                              double z, double yaw,
                                              RuntimePlayerStartRouteSpawnResult* result)
        -> UObject*
    {
        SavePointSpawnRequest request{};
        request.request_id = STR("fz02-selector-player-start");
        request.player_start_tag = std::wstring{player_start_tag};
        request.runtime_player_start_tag = std::wstring{player_start_tag};
        request.disp_map_name = std::wstring{disp_map_name};
        request.is_auto_save = true;
        request.spawn_runtime_player_start = true;

        RuntimePlayerStartSpawnSummary internal_summary{};
        UObject* const actor = spawn_runtime_player_start(
            request, world_context, SpawnVector{x, y, z}, &internal_summary, yaw);
        if (result)
        {
            result->status = internal_summary.status;
            result->reason = internal_summary.reason;
            result->tag = internal_summary.tag;
            result->class_resolution_source = internal_summary.class_resolution_source;
            result->actor = internal_summary.actor;
            result->player_start_property = internal_summary.player_start_property;
        }
        return actor;
    }

    auto sync_goa_save_point_route(const RuntimeSavePointSpawnDeps& deps,
                                   std::wstring_view slot_name,
                                   std::wstring_view level_path_property,
                                   std::wstring_view player_start_property,
                                   std::wstring_view level_path, std::wstring_view player_start,
                                   GoASavePointRouteSyncSummary* summary) -> bool
    {
        SavePointSpawnRequest request{};
        request.sync_goa_save_point_slot = true;
        request.goa_save_point_slot_name = std::wstring{slot_name};
        request.goa_save_point_level_path_property = std::wstring{level_path_property};
        request.goa_save_point_player_start_property = std::wstring{player_start_property};
        request.goa_save_point_level_path = std::wstring{level_path};
        request.goa_save_point_player_start = std::wstring{player_start};

        GoASavePointSyncSummary internal_summary{};
        const bool synced = sync_goa_save_point_save_game(deps, request, &internal_summary);
        if (summary)
        {
            summary->requested = internal_summary.requested;
            summary->loaded = internal_summary.loaded;
            summary->created = internal_summary.created;
            summary->saved = internal_summary.saved;
            summary->status = internal_summary.status;
            summary->reason = internal_summary.reason;
            summary->slot_name = internal_summary.slot_name;
            summary->class_resolution_source = internal_summary.class_resolution_source;
            summary->save_game_object = internal_summary.save_game_object;
            summary->level_path = internal_summary.level_path;
            summary->player_start = internal_summary.player_start;
        }
        return synced;
    }

    auto process_pending_runtime_save_point_spawn(const RuntimeSavePointSpawnDeps& deps) -> void
    {
        if (!deps.bridge_dir.has_value())
        {
            return;
        }

        if (deps.enable_lifetime_deep_samples)
        {
            poll_save_point_spawn_watches(deps);
        }

        const auto request_path =
            *deps.bridge_dir / std::filesystem::path{std::wstring{deps.request_filename}};
        const auto request_text = read_text_file(request_path);
        if (!request_text.has_value())
        {
            return;
        }

        SavePointSpawnRequest request = parse_request(*request_text, deps);
        if (request.request_id.empty())
        {
            const std::string observed_at = now_iso8601_utc();
            request.request_id = std::wstring{STR("manual-save-point-")} +
                                 std::wstring{observed_at.begin(), observed_at.end()};
        }

        if (deps.runtime_state.grant_state().special_battle_mode_active())
        {
            write_result(deps, request, STR("waiting"), STR("special_battle_mode_active"), nullptr,
                         nullptr, std::nullopt, STR(""), STR(""), nullptr, nullptr, nullptr,
                         nullptr);
            return;
        }

        UObject* const pawn = deps.runtime_state.grant_state().pawn_state().object();
        if (!pawn || !kh3ap::is_uobject_valid_fast(pawn) ||
            !RuntimeObjectResolver::is_sora_ability_pawn_candidate(pawn))
        {
            write_result(deps, request, STR("waiting"), STR("missing_safe_sora_pawn"), pawn,
                         nullptr, std::nullopt, STR(""), STR(""), nullptr, nullptr, nullptr,
                         nullptr);
            return;
        }

        std::wstring location_failure{};
        std::optional<SpawnVector> location = request.location;
        if (!location.has_value())
        {
            location =
                try_get_actor_location(pawn, deps.actor_location_function_path, &location_failure);
        }
        if (!location.has_value())
        {
            write_result(deps, request, STR("failed"),
                         location_failure.empty() ? std::wstring_view{STR("missing_spawn_location")}
                                                  : std::wstring_view{location_failure},
                         pawn, nullptr, std::nullopt, STR(""), STR(""), nullptr, nullptr, nullptr,
                         nullptr);
            try_remove_file(request_path);
            return;
        }

        std::wstring class_resolution_source{};
        std::wstring player_start_property{};
        std::array<ComponentAssignmentSummary, 3> component_assignments{};
        SavePointSetupSummary setup_summary{};
        RuntimePlayerStartSpawnSummary runtime_player_start_summary{};
        UObject* const runtime_player_start = spawn_runtime_player_start(
            request, pawn, *location, &runtime_player_start_summary, 0.0);

        SavePointSpawnRequest effective_request = request;
        if (request.spawn_runtime_player_start &&
            request.use_runtime_player_start_tag_for_save_point && runtime_player_start &&
            kh3ap::is_uobject_valid_fast(runtime_player_start) &&
            !runtime_player_start_summary.tag.empty())
        {
            effective_request.player_start_tag = runtime_player_start_summary.tag;
            effective_request.runtime_player_start_tag = runtime_player_start_summary.tag;
        }

        std::wstring failure_reason{};
        UObject* const spawned_actor = spawn_save_point(
            deps, effective_request, pawn, *location, &class_resolution_source,
            &player_start_property, &component_assignments, &setup_summary, &failure_reason);

        // spawn_save_point already validates the deferred spawn result before
        // returning, so do not immediately repeat a global UObject scan.
        const bool spawned = spawned_actor != nullptr;
        write_result(
            deps, effective_request,
            spawned ? std::wstring_view{STR("spawned")} : std::wstring_view{STR("failed")},
            spawned ? std::wstring_view{STR("")} : std::wstring_view{failure_reason}, pawn,
            spawned_actor, location, class_resolution_source, player_start_property,
            &runtime_player_start_summary,
            spawned && deps.enable_lifetime_deep_samples ? &component_assignments : nullptr,
            spawned && deps.enable_lifetime_deep_samples ? &setup_summary : nullptr, nullptr);
        if (spawned && deps.enable_lifetime_deep_samples)
        {
            register_spawn_watch(SavePointSpawnWatch{
                STR("manual_request"), effective_request.request_id, spawned_actor,
                format_object_debug_name(spawned_actor), runtime_player_start,
                format_object_debug_name(runtime_player_start), std::chrono::steady_clock::now(),
                0});
        }
        try_remove_file(request_path);

        if (deps.debug_logging)
        {
            Output::send<LogLevel::Verbose>(
                STR("[KH3APProbeCpp] RuntimeSavePointSpawn status={} actor={} reason={} "
                    "request_id={}\n"),
                spawned ? STR("spawned") : STR("failed"), format_object_debug_name(spawned_actor),
                failure_reason.empty() ? std::wstring{STR("<none>")} : failure_reason,
                effective_request.request_id);
        }
    }

    auto process_runtime_map_save_point_auto_spawn(
        RuntimeAutoSavePointSpawnState& state, const RuntimeSavePointSpawnDeps& deps,
        std::chrono::steady_clock::time_point now, std::wstring_view source_name,
        std::wstring_view request_id_prefix, std::wstring_view map_path,
        std::wstring_view player_start_tag, std::wstring_view disp_map_name, double x, double y,
        double z, std::chrono::steady_clock::duration stable_delay,
        std::chrono::steady_clock::duration retry_interval,
        std::wstring_view goa_save_point_slot_name,
        std::wstring_view goa_save_point_level_path_property,
        std::wstring_view goa_save_point_player_start_property) -> void
    {
        if (!deps.bridge_dir.has_value())
        {
            return;
        }

        if (deps.enable_lifetime_deep_samples)
        {
            poll_save_point_spawn_watches(deps);
        }

        auto& context_state = deps.runtime_state.context_subsystem_state().context_state();
        const auto snapshot = context_state.current_snapshot();
        if (!snapshot.has_value())
        {
            return;
        }

        auto& pawn_state = deps.runtime_state.grant_state().pawn_state();
        UObject* const pawn = pawn_state.object();
        const bool playable_context_matches_pawn = runtime_save_point_playable_context_matches_pawn(
            snapshot->hook, context_state.last_playable_context_object(), pawn);

        const std::wstring context_text =
            snapshot->world + STR(" ") + snapshot->outer + STR(" ") + snapshot->object;
        if (!runtime_context_matches_map(context_text, map_path))
        {
            // Camera, UI, and streamed-sublevel callbacks can temporarily own
            // the latest context while the same base map and pawn remain live.
            // Clear only when a trusted playable-pawn context proves that a
            // different map is current; hard map/player starts also reset the
            // lifecycle explicitly.
            if (pawn_state.candidate_trusted() && playable_context_matches_pawn)
            {
                state.reset();
            }
            return;
        }

        if (!pawn || !pawn_state.candidate_trusted() || !kh3ap::is_uobject_valid_fast(pawn) ||
            !playable_context_matches_pawn)
        {
            return;
        }

        const std::wstring context_key = auto_spawn_context_key(map_path);

        // Candidate discovery validated this pawn before caching it, the fast
        // object-array check above rejects teardown state in constant time,
        // and the playable context carries the same stable UObject identity.
        // Its diagnostic name may be refreshed independently by later hooks.
        // Avoid the old full-array IsReal/GetFullName walk on this periodic
        // path. A replacement pawn does not rearm spawning in the same visit.
        if (state.finished_for(context_key))
        {
            return;
        }

        state.begin_context(context_key);

        if (retry_interval.count() > 0 && state.last_attempt_at.time_since_epoch().count() != 0 &&
            now - state.last_attempt_at < retry_interval)
        {
            return;
        }
        if (deps.runtime_state.grant_state().special_battle_mode_active())
        {
            return;
        }

        const auto stable_for = pawn_state.stable_for(now);
        if (stable_delay.count() > 0 && stable_for < stable_delay)
        {
            return;
        }

        state.last_attempt_at = now;
        if (deps.auto_spawn_allowed && !deps.auto_spawn_allowed())
        {
            return;
        }
        ++state.attempt_count;
        const auto operation_started_at = std::chrono::steady_clock::now();

        SavePointSpawnRequest request{};
        request.request_id = std::wstring{request_id_prefix};
        const std::wstring request_suffix =
            sanitize_name_token(snapshot->opened_at, runtime_name_suffix_limit);
        if (!request_suffix.empty())
        {
            request.request_id += STR("-");
            request.request_id += request_suffix;
        }
        request.player_start_tag = std::wstring{player_start_tag};
        request.disp_map_name = std::wstring{disp_map_name};
        request.my_reactor_path = std::wstring{deps.default_my_reactor_path};
        request.my_recover_path = std::wstring{deps.default_my_recover_path};
        request.my_map_marker_path = std::wstring{deps.default_my_map_marker_path};
        request.location = SpawnVector{x, y, z};
        request.is_auto_save = true;
        request.force_activate = true;
        request.force_component_engine_activation = true;
        request.disable_blocking_collision = true;
        request.spawn_runtime_player_start = false;
        request.use_runtime_player_start_tag_for_save_point = false;
        request.sync_goa_save_point_slot = !goa_save_point_slot_name.empty();
        request.goa_save_point_slot_name = std::wstring{goa_save_point_slot_name};
        request.goa_save_point_level_path_property =
            std::wstring{goa_save_point_level_path_property};
        request.goa_save_point_player_start_property =
            std::wstring{goa_save_point_player_start_property};
        request.goa_save_point_level_path = std::wstring{map_path};
        request.goa_save_point_player_start = std::wstring{player_start_tag};

        std::wstring class_resolution_source{};
        std::wstring player_start_property{};
        std::array<ComponentAssignmentSummary, 3> component_assignments{};
        SavePointSetupSummary setup_summary{};
        std::wstring failure_reason{};
        UObject* const spawned_actor = spawn_save_point(
            deps, request, pawn, *request.location, &class_resolution_source,
            &player_start_property, &component_assignments, &setup_summary, &failure_reason);
        const auto spawn_completed_at = std::chrono::steady_clock::now();

        // spawn_save_point already validates the deferred spawn result before
        // returning, so do not immediately repeat a global UObject scan.
        const bool spawned = spawned_actor != nullptr;
        const bool class_not_loaded =
            !spawned && class_resolution_source == STR("save_point_class_not_loaded");
        bool class_load_requested_now = false;
        if (class_not_loaded && !state.asset_load_requested)
        {
            std::wstring load_source{};
            class_load_requested_now = request_runtime_asset_class_load(
                pawn, deps.save_point_class_path, &load_source, 0x4b483303);
            state.asset_load_requested = class_load_requested_now;
            if (!load_source.empty())
            {
                class_resolution_source += STR("|");
                class_resolution_source += load_source;
            }
            failure_reason = class_load_requested_now
                                 ? std::wstring{STR("save_point_class_load_requested")}
                                 : std::wstring{STR("save_point_class_load_request_failed")};
        }
        const auto validation_completed_at = std::chrono::steady_clock::now();
        GoASavePointSyncSummary goa_save_point_sync_summary{};
        if (request.sync_goa_save_point_slot)
        {
            if (spawned)
            {
                (void)sync_goa_save_point_save_game(deps, request, &goa_save_point_sync_summary);
            }
            else
            {
                goa_save_point_sync_summary.requested = true;
                goa_save_point_sync_summary.status = STR("skipped");
                goa_save_point_sync_summary.reason = STR("spawn_failed");
                goa_save_point_sync_summary.slot_name = request.goa_save_point_slot_name;
                goa_save_point_sync_summary.level_path = request.goa_save_point_level_path;
                goa_save_point_sync_summary.player_start = request.goa_save_point_player_start;
            }
        }
        const auto goa_sync_completed_at = std::chrono::steady_clock::now();
        if (spawned)
        {
            state.spawn_attempted = true;
            state.spawned_actor = spawned_actor;
            state.spawned_actor_name = format_object_debug_name(spawned_actor);
            if (deps.enable_lifetime_deep_samples)
            {
                register_spawn_watch(SavePointSpawnWatch{
                    std::wstring{source_name}, request.request_id, spawned_actor,
                    state.spawned_actor_name, nullptr, format_object_debug_name(nullptr),
                    std::chrono::steady_clock::now(), 0});
            }
        }
        else
        {
            state.spawn_attempted = false;
            state.spawned_actor = nullptr;
            state.spawned_actor_name.clear();
            // One bounded follow-up is enough for a latent class-load request
            // or a transient BeginDeferredActorSpawn failure. Never continue
            // touching UObject state every five seconds through traversal,
            // cinematics, or a boss transition.
            const bool retry_armed = should_retry_runtime_save_point_auto_spawn(
                spawned, class_not_loaded, class_load_requested_now, state.attempt_count);
            state.terminal_failure = !retry_armed;
        }

        const bool waiting_for_retry = !spawned && !state.terminal_failure;
        const std::wstring_view result_status = spawned ? std::wstring_view{STR("spawned")}
                                                : waiting_for_retry
                                                    ? std::wstring_view{STR("waiting")}
                                                    : std::wstring_view{STR("failed")};

        write_result(deps, request, result_status,
                     spawned ? std::wstring_view{STR("")} : std::wstring_view{failure_reason}, pawn,
                     spawned_actor, request.location, class_resolution_source,
                     player_start_property, nullptr,
                     spawned && deps.enable_lifetime_deep_samples ? &component_assignments
                                                                  : nullptr,
                     spawned && deps.enable_lifetime_deep_samples ? &setup_summary : nullptr,
                     request.sync_goa_save_point_slot ? &goa_save_point_sync_summary : nullptr);
        const auto result_write_completed_at = std::chrono::steady_clock::now();

        const auto elapsed_us = [](auto begin, auto end) -> uint64_t
        {
            return static_cast<uint64_t>(
                std::chrono::duration_cast<std::chrono::microseconds>(end - begin).count());
        };

        Output::send<LogLevel::Verbose>(
            STR("[KH3APProbeCpp] RuntimeMapSavePointAutoSpawn source={} status={} actor={} "
                "reason={} context={} stable_ms={} attempt={} post_spawn_validation=skipped "
                "retry_armed={} terminal_failure={} goa_sync={} total_us={} spawn_us={} "
                "validation_us={} goa_sync_us={} result_write_us={} deep_result={}\n"),
            source_name, result_status, format_object_debug_name(spawned_actor),
            failure_reason.empty() ? std::wstring{STR("<none>")} : failure_reason, context_key,
            static_cast<uint64_t>(
                std::chrono::duration_cast<std::chrono::milliseconds>(stable_for).count()),
            state.attempt_count, waiting_for_retry ? STR("true") : STR("false"),
            state.terminal_failure ? STR("true") : STR("false"),
            request.sync_goa_save_point_slot ? goa_save_point_sync_summary.status
                                             : std::wstring{STR("not_requested")},
            elapsed_us(operation_started_at, result_write_completed_at),
            elapsed_us(operation_started_at, spawn_completed_at),
            elapsed_us(spawn_completed_at, validation_completed_at),
            elapsed_us(validation_completed_at, goa_sync_completed_at),
            elapsed_us(goa_sync_completed_at, result_write_completed_at),
            deps.enable_lifetime_deep_samples ? STR("true") : STR("false"));
    }
} // namespace kh3ap
