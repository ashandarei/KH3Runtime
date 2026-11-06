#include "kh3ap/features/world/portals/portal_spawn_helpers.h"
#include "kh3ap/features/world/portals/portal_spawn.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/bridge/utilities.h"
#include <UnrealDef.hpp>
#include <Unreal/UObject.hpp>
#include <string>
#include "kh3ap/features/world/portals/portal_result_writer.h"
#include "kh3ap/bridge/serialization/json_fields.h"
#include "kh3ap/bridge/transport/bridge_io_helpers.h"
#include "kh3ap/features/player/context/context_helpers.h"
#include "kh3ap/features/grants/grant_state.h"
#include "kh3ap/features/player/objects/object_resolver.h"
#include "kh3ap/app/state.h"
#include <DynamicOutput/DynamicOutput.hpp>
#include <chrono>
#include <filesystem>
#include <utility>

namespace kh3ap
{
    using namespace RC;
    using namespace RC::Unreal;
    using namespace portal_spawn_detail;
    namespace
    {
        struct PortalSpawnRequest
        {
            std::wstring request_id{};
            std::wstring map_path{};
            std::optional<PortalSpawnVector> location{};
            bool require_map_match{true};
            bool use_pawn_location{true};
        };

        auto parse_portal_request(const std::string& text) -> PortalSpawnRequest
        {
            PortalSpawnRequest request{};
            request.request_id =
                JsonFieldReader::string_field(text, "request_id").value_or(std::wstring{});
            request.map_path =
                JsonFieldReader::string_field(text, "map_path").value_or(std::wstring{});
            request.require_map_match =
                JsonFieldReader::bool_field(text, "require_map_match").value_or(true);
            request.use_pawn_location =
                JsonFieldReader::bool_field(text, "use_pawn_location").value_or(true);

            const auto x = JsonFieldReader::number_field(text, "x");
            const auto y = JsonFieldReader::number_field(text, "y");
            const auto z = JsonFieldReader::number_field(text, "z");
            if (x.has_value() && y.has_value() && z.has_value())
            {
                request.location = PortalSpawnVector{*x, *y, *z};
            }
            return request;
        }

        auto write_portal_result(const RuntimePortalSpawnDeps& deps, std::wstring_view source_name,
                                 std::wstring_view request_id, std::wstring_view status,
                                 std::wstring_view reason, UObject* pawn, UObject* spawned_actor,
                                 const PortalSpawnVector& location,
                                 std::wstring_view class_resolution_source,
                                 std::wstring_view map_path, std::wstring_view context_key,
                                 std::wstring_view runtime_context, uint64_t stable_ms) -> void
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
            result.status = status;
            result.reason = reason;
            result.source_name = source_name;
            result.request_id = request_id;
            result.build_stamp = deps.build_stamp;
            result.map_path = map_path;
            result.context_key = context_key;
            result.runtime_context = runtime_context;
            result.pawn_name = format_object_debug_name(pawn);
            result.spawned_actor_name = format_object_debug_name(spawned_actor);
            result.portal_class_path = deps.portal_class_path;
            result.portal_blueprint_path = deps.portal_blueprint_path;
            result.class_resolution_source = class_resolution_source;
            result.pawn_address = reinterpret_cast<uintptr_t>(pawn);
            result.spawned_actor_address = reinterpret_cast<uintptr_t>(spawned_actor);
            result.stable_ms = stable_ms;
            result.x = location.x;
            result.y = location.y;
            result.z = location.z;
            write_portal_spawn_result(result);
        }
    } // namespace

    auto process_pending_runtime_portal_spawn(const RuntimePortalSpawnDeps& deps) -> void
    {
        if (!deps.bridge_dir.has_value())
        {
            return;
        }

        const auto request_path =
            *deps.bridge_dir / std::filesystem::path{std::wstring{deps.request_filename}};
        const auto request_text = read_text_file(request_path);
        if (!request_text.has_value())
        {
            return;
        }

        PortalSpawnRequest request = parse_portal_request(*request_text);
        if (request.request_id.empty())
        {
            const std::string observed_at = now_iso8601_utc();
            request.request_id = std::wstring{STR("manual-portal-")} +
                                 std::wstring{observed_at.begin(), observed_at.end()};
        }

        const auto snapshot =
            deps.runtime_state.context_subsystem_state().context_state().current_snapshot();
        std::wstring context_text{};
        std::wstring context_key{};
        std::wstring effective_map_path{request.map_path};
        if (snapshot.has_value())
        {
            context_text =
                snapshot->world + STR(" ") + snapshot->outer + STR(" ") + snapshot->object;
            context_key = snapshot->world + STR("|") + snapshot->object;
            if (effective_map_path.empty())
            {
                effective_map_path = snapshot->world;
            }
        }

        const std::wstring runtime_context =
            runtime_context_summary(deps.runtime_state, deps.bridge_dir);
        const PortalSpawnVector requested_or_empty_location =
            request.location.value_or(PortalSpawnVector{});

        auto write_manual_result =
            [&](std::wstring_view status, std::wstring_view reason, UObject* pawn,
                UObject* spawned_actor, const PortalSpawnVector& location,
                std::wstring_view class_resolution_source, uint64_t stable_ms)
        {
            write_portal_result(deps, STR("manual_portal_request"), request.request_id, status,
                                reason, pawn, spawned_actor, location, class_resolution_source,
                                effective_map_path, context_key, runtime_context, stable_ms);
        };

        if (request.require_map_match && !request.map_path.empty())
        {
            if (!snapshot.has_value())
            {
                write_manual_result(STR("waiting"), STR("missing_runtime_context_for_map_match"),
                                    nullptr, nullptr, requested_or_empty_location, STR(""), 0);
                return;
            }
            if (context_text.find(request.map_path) == std::wstring::npos)
            {
                write_manual_result(STR("waiting"), STR("current_map_does_not_match_request"),
                                    nullptr, nullptr, requested_or_empty_location, STR(""), 0);
                return;
            }
        }

        if (deps.runtime_state.grant_state().special_battle_mode_active())
        {
            write_manual_result(STR("waiting"), STR("special_battle_mode_active"), nullptr, nullptr,
                                requested_or_empty_location, STR(""), 0);
            return;
        }

        auto& pawn_state = deps.runtime_state.grant_state().pawn_state();
        UObject* const pawn = pawn_state.object();
        if (!pawn || !kh3ap::is_uobject_valid_fast(pawn) ||
            !RuntimeObjectResolver::is_sora_ability_pawn_candidate(pawn))
        {
            write_manual_result(STR("waiting"), STR("missing_safe_sora_pawn"), pawn, nullptr,
                                requested_or_empty_location, STR(""), 0);
            return;
        }

        const auto now = std::chrono::steady_clock::now();
        const auto stable_for = pawn_state.stable_for(now);
        const uint64_t stable_ms = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(stable_for).count());

        std::wstring location_failure{};
        std::optional<PortalSpawnVector> location = request.location;
        if (!location.has_value() && request.use_pawn_location)
        {
            location =
                try_get_actor_location(pawn, deps.actor_location_function_path, &location_failure);
        }
        if (!location.has_value())
        {
            write_manual_result(STR("failed"),
                                location_failure.empty()
                                    ? std::wstring_view{STR("missing_spawn_location")}
                                    : std::wstring_view{location_failure},
                                pawn, nullptr, requested_or_empty_location, STR(""), stable_ms);
            try_remove_file(request_path);
            return;
        }

        std::wstring class_resolution_source{};
        std::wstring failure_reason{};
        UObject* const spawned_actor =
            spawn_portal(deps, pawn, *location, true, &class_resolution_source, &failure_reason);

        const bool spawned = spawned_actor && kh3ap::is_uobject_valid_fast(spawned_actor);
        const bool load_requested =
            !spawned &&
            class_resolution_source.find(STR("kismet_load_asset")) != std::wstring::npos &&
            class_resolution_source.find(STR("requested")) != std::wstring::npos;
        write_manual_result(spawned          ? std::wstring_view{STR("spawned")}
                            : load_requested ? std::wstring_view{STR("waiting")}
                                             : std::wstring_view{STR("failed")},
                            spawned          ? std::wstring_view{STR("")}
                            : load_requested ? std::wstring_view{STR("asset_load_requested")}
                                             : std::wstring_view{failure_reason},
                            pawn, spawned_actor, *location, class_resolution_source, stable_ms);

        if (!load_requested)
        {
            try_remove_file(request_path);
        }

        if (deps.debug_logging)
        {
            Output::send<LogLevel::Verbose>(
                STR("[KH3APProbeCpp] RuntimePortalSpawnRequest status={} actor={} reason={} "
                    "request_id={} x={} y={} z={}\n"),
                spawned ? STR("spawned") : (load_requested ? STR("waiting") : STR("failed")),
                format_object_debug_name(spawned_actor),
                failure_reason.empty() ? std::wstring{STR("<none>")} : failure_reason,
                request.request_id, location->x, location->y, location->z);
        }
    }
} // namespace kh3ap
