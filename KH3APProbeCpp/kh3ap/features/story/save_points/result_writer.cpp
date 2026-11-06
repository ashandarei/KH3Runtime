#include "kh3ap/features/story/save_points/internal.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/bridge/utilities.h"
#include "kh3ap/bridge/transport/bridge_io_helpers.h"
#include <UnrealDef.hpp>
#include <Unreal/FProperty.hpp>
#include <Unreal/UClass.hpp>
#include <Unreal/UFunction.hpp>
#include <Unreal/UObject.hpp>
#include <Unreal/UObjectGlobals.hpp>
#include <array>
#include <cstddef>
#include <filesystem>
#include <optional>
#include <sstream>
#include <string>

namespace kh3ap::save_point_detail
{
    using namespace RC;
    using namespace RC::Unreal;
    using save_point_activation::ActivationCallSummary;
    using save_point_native::describe_object_property;
    using save_point_text::append_json_number_field;
    using save_point_text::append_json_string_field;

    auto append_setup_summary_json(std::ostringstream& json,
                                   const SavePointSetupSummary* setup_summary) -> void
    {
        if (!setup_summary)
        {
            return;
        }

        json << ",\"metadata_writes\":[";
        for (size_t index = 0; index < setup_summary->metadata_writes.size(); ++index)
        {
            const MetadataWriteSummary& summary = setup_summary->metadata_writes[index];
            if (index > 0)
            {
                json << ",";
            }
            json << "{";
            json << "\"stage\":\"" << json_escape(summary.stage) << "\"";
            json << ",\"property_name\":\"" << json_escape(summary.property_name) << "\"";
            json << ",\"requested_value\":\"" << json_escape(summary.requested_value) << "\"";
            json << ",\"status\":\"" << json_escape(summary.status) << "\"";
            json << ",\"wrote\":" << (summary.wrote ? "true" : "false");
            json << ",\"final_value\":\"" << json_escape(summary.final_value) << "\"";
            if (!summary.reason.empty())
            {
                json << ",\"reason\":\"" << json_escape(summary.reason) << "\"";
            }
            json << "}";
        }
        json << "]";

        json << ",\"activation_calls\":[";
        for (size_t index = 0; index < setup_summary->activation_calls.size(); ++index)
        {
            const ActivationCallSummary& summary = setup_summary->activation_calls[index];
            if (index > 0)
            {
                json << ",";
            }
            json << "{";
            json << "\"function_path\":\"" << json_escape(summary.function_path) << "\"";
            json << ",\"status\":\"" << json_escape(summary.status) << "\"";
            json << ",\"invoked\":" << (summary.invoked ? "true" : "false");
            json << ",\"elapsed_us\":" << summary.elapsed_us;
            if (!summary.param_name.empty())
            {
                json << ",\"param_name\":\"" << json_escape(summary.param_name) << "\"";
                json << ",\"param_value\":\"" << json_escape(summary.param_value) << "\"";
                json << ",\"param_written\":" << (summary.param_written ? "true" : "false");
            }
            if (!summary.reason.empty())
            {
                json << ",\"reason\":\"" << json_escape(summary.reason) << "\"";
            }
            json << "}";
        }
        json << "]";
    }

    auto append_goa_save_point_sync_summary_json(std::ostringstream& json,
                                                 const GoASavePointSyncSummary* sync_summary)
        -> void
    {
        if (!sync_summary)
        {
            return;
        }

        json << ",\"goa_save_point_sync\":{";
        json << "\"requested\":" << (sync_summary->requested ? "true" : "false");
        json << ",\"loaded\":" << (sync_summary->loaded ? "true" : "false");
        json << ",\"created\":" << (sync_summary->created ? "true" : "false");
        json << ",\"saved\":" << (sync_summary->saved ? "true" : "false");
        json << ",\"status\":\"" << json_escape(sync_summary->status) << "\"";
        json << ",\"reason\":\"" << json_escape(sync_summary->reason) << "\"";
        json << ",\"slot_name\":\"" << json_escape(sync_summary->slot_name) << "\"";
        json << ",\"class_resolution_source\":\""
             << json_escape(sync_summary->class_resolution_source) << "\"";
        json << ",\"save_game_object\":\"" << json_escape(sync_summary->save_game_object) << "\"";
        json << ",\"level_path\":\"" << json_escape(sync_summary->level_path) << "\"";
        json << ",\"player_start\":\"" << json_escape(sync_summary->player_start) << "\"";
        json << ",\"property_writes\":[";
        for (size_t index = 0; index < sync_summary->property_writes.size(); ++index)
        {
            const MetadataWriteSummary& summary = sync_summary->property_writes[index];
            if (index > 0)
            {
                json << ",";
            }
            json << "{";
            json << "\"stage\":\"" << json_escape(summary.stage) << "\"";
            json << ",\"property_name\":\"" << json_escape(summary.property_name) << "\"";
            json << ",\"requested_value\":\"" << json_escape(summary.requested_value) << "\"";
            json << ",\"status\":\"" << json_escape(summary.status) << "\"";
            json << ",\"wrote\":" << (summary.wrote ? "true" : "false");
            json << ",\"final_value\":\"" << json_escape(summary.final_value) << "\"";
            if (!summary.reason.empty())
            {
                json << ",\"reason\":\"" << json_escape(summary.reason) << "\"";
            }
            json << "}";
        }
        json << "]";
        json << "}";
    }

    auto write_result(const RuntimeSavePointSpawnDeps& deps, const SavePointSpawnRequest& request,
                      std::wstring_view status, std::wstring_view reason, UObject* pawn,
                      UObject* actor, const std::optional<SpawnVector>& location,
                      std::wstring_view class_resolution_source,
                      std::wstring_view player_start_property,
                      const RuntimePlayerStartSpawnSummary* runtime_player_start_summary,
                      const std::array<ComponentAssignmentSummary, 3>* component_assignments,
                      const SavePointSetupSummary* setup_summary,
                      const GoASavePointSyncSummary* goa_save_point_sync_summary) -> void
    {
        if (!deps.bridge_dir.has_value())
        {
            return;
        }

        std::ostringstream json;
        json << "{";
        json << "\"schema\":\"kh3ap.runtime_save_point_spawn_result.v1\"";
        json << ",\"protocol_name\":\"" << json_escape(deps.protocol_name) << "\"";
        json << ",\"protocol_version\":" << deps.protocol_version;
        json << ",\"event\":\"runtime_save_point_spawn_result\"";
        append_json_string_field(json, "status", status);
        append_json_string_field(json, "reason", reason);
        append_json_string_field(json, "request_id", request.request_id);
        const std::string observed_at = now_iso8601_utc();
        append_json_string_field(json, "observed_at_utc",
                                 std::wstring{observed_at.begin(), observed_at.end()});
        append_json_string_field(json, "build_stamp", deps.build_stamp);
        append_json_string_field(json, "save_point_class_path", deps.save_point_class_path);
        append_json_string_field(json, "save_point_blueprint_path", deps.save_point_blueprint_path);
        append_json_string_field(json, "class_resolution_source", class_resolution_source);
        append_json_string_field(json, "player_start_tag", request.player_start_tag);
        append_json_string_field(json, "player_start_property", player_start_property);
        append_json_string_field(json, "disp_map_name", request.disp_map_name);
        json << ",\"is_auto_save\":" << (request.is_auto_save ? "true" : "false");
        json << ",\"force_activate\":" << (request.force_activate ? "true" : "false");
        json << ",\"force_component_engine_activation\":"
             << (request.force_component_engine_activation ? "true" : "false");
        json << ",\"disable_blocking_collision\":"
             << (request.disable_blocking_collision ? "true" : "false");
        json << ",\"spawn_runtime_player_start\":"
             << (request.spawn_runtime_player_start ? "true" : "false");
        json << ",\"use_runtime_player_start_tag_for_save_point\":"
             << (request.use_runtime_player_start_tag_for_save_point ? "true" : "false");
        append_json_string_field(json, "runtime_player_start_tag",
                                 request.runtime_player_start_tag);
        append_json_string_field(json, "my_reactor_path", request.my_reactor_path);
        append_json_string_field(json, "my_recover_path", request.my_recover_path);
        append_json_string_field(json, "my_map_marker_path", request.my_map_marker_path);
        append_json_string_field(json, "pawn", format_object_debug_name(pawn));
        append_json_string_field(json, "spawned_actor", format_object_debug_name(actor));
        if (location.has_value())
        {
            append_json_number_field(json, "x", location->x);
            append_json_number_field(json, "y", location->y);
            append_json_number_field(json, "z", location->z);
        }
        // The full reflected actor/component dump performs dozens of
        // ProcessEvent/property inspections. It is useful for a targeted
        // lifetime diagnostic, but can stall the game thread for seconds
        // when emitted as part of a normal runtime auto-spawn result.
        if (deps.enable_lifetime_deep_samples && actor && kh3ap::is_uobject_valid_fast(actor))
        {
            json << ",\"components\":{";
            json << "\"MyReactor\":\""
                 << json_escape(describe_object_property(actor, STR("MyReactor"))) << "\"";
            json << ",\"MyRecover\":\""
                 << json_escape(describe_object_property(actor, STR("MyRecover"))) << "\"";
            json << ",\"MyMapMarker\":\""
                 << json_escape(describe_object_property(actor, STR("MyMapMarker"))) << "\"";
            json << "}";
            append_save_point_deep_state_json(json, actor);
        }
        if (runtime_player_start_summary)
        {
            json << ",\"runtime_player_start\":{";
            json << "\"requested\":"
                 << (runtime_player_start_summary->requested ? "true" : "false");
            json << ",\"status\":\"" << json_escape(runtime_player_start_summary->status) << "\"";
            json << ",\"reason\":\"" << json_escape(runtime_player_start_summary->reason) << "\"";
            json << ",\"tag\":\"" << json_escape(runtime_player_start_summary->tag) << "\"";
            json << ",\"class_resolution_source\":\""
                 << json_escape(runtime_player_start_summary->class_resolution_source) << "\"";
            json << ",\"actor\":\"" << json_escape(runtime_player_start_summary->actor) << "\"";
            json << ",\"player_start_property\":\""
                 << json_escape(runtime_player_start_summary->player_start_property) << "\"";
            json << ",\"metadata_writes\":[";
            for (size_t index = 0; index < runtime_player_start_summary->metadata_writes.size();
                 ++index)
            {
                const MetadataWriteSummary& summary =
                    runtime_player_start_summary->metadata_writes[index];
                if (index > 0)
                {
                    json << ",";
                }
                json << "{";
                json << "\"stage\":\"" << json_escape(summary.stage) << "\"";
                json << ",\"property_name\":\"" << json_escape(summary.property_name) << "\"";
                json << ",\"requested_value\":\"" << json_escape(summary.requested_value) << "\"";
                json << ",\"status\":\"" << json_escape(summary.status) << "\"";
                json << ",\"wrote\":" << (summary.wrote ? "true" : "false");
                json << ",\"final_value\":\"" << json_escape(summary.final_value) << "\"";
                if (!summary.reason.empty())
                {
                    json << ",\"reason\":\"" << json_escape(summary.reason) << "\"";
                }
                json << "}";
            }
            json << "]";
            json << ",\"activation_calls\":[";
            for (size_t index = 0; index < runtime_player_start_summary->activation_calls.size();
                 ++index)
            {
                const ActivationCallSummary& summary =
                    runtime_player_start_summary->activation_calls[index];
                if (index > 0)
                {
                    json << ",";
                }
                json << "{";
                json << "\"function_path\":\"" << json_escape(summary.function_path) << "\"";
                json << ",\"status\":\"" << json_escape(summary.status) << "\"";
                json << ",\"invoked\":" << (summary.invoked ? "true" : "false");
                json << ",\"elapsed_us\":" << summary.elapsed_us;
                if (!summary.param_name.empty())
                {
                    json << ",\"param_name\":\"" << json_escape(summary.param_name) << "\"";
                    json << ",\"param_value\":\"" << json_escape(summary.param_value) << "\"";
                    json << ",\"param_written\":" << (summary.param_written ? "true" : "false");
                }
                if (!summary.reason.empty())
                {
                    json << ",\"reason\":\"" << json_escape(summary.reason) << "\"";
                }
                json << "}";
            }
            json << "]";
            json << "}";
        }
        if (component_assignments)
        {
            json << ",\"component_assignments\":{";
            for (size_t index = 0; index < component_assignments->size(); ++index)
            {
                const ComponentAssignmentSummary& summary = (*component_assignments)[index];
                if (index > 0)
                {
                    json << ",";
                }
                json << "\"" << json_escape(summary.property_name) << "\":{";
                json << "\"status\":\"" << json_escape(summary.status) << "\"";
                json << ",\"wrote\":" << (summary.wrote ? "true" : "false");
                json << ",\"requested_path\":\"" << json_escape(summary.requested_path) << "\"";
                json << ",\"lookup_path\":\"" << json_escape(summary.lookup_path) << "\"";
                json << ",\"resolved_object\":\"" << json_escape(summary.resolved_object) << "\"";
                json << ",\"final_value\":\"" << json_escape(summary.final_value) << "\"";
                if (!summary.reason.empty())
                {
                    json << ",\"reason\":\"" << json_escape(summary.reason) << "\"";
                }
                json << "}";
            }
            json << "}";
        }
        append_setup_summary_json(json, setup_summary);
        append_goa_save_point_sync_summary_json(json, goa_save_point_sync_summary);
        json << "}\n";

        write_text_file(*deps.bridge_dir /
                            std::filesystem::path{std::wstring{deps.result_filename}},
                        json.str());
        append_text_file(*deps.bridge_dir /
                             std::filesystem::path{std::wstring{deps.events_filename}},
                         json.str());
    }
} // namespace kh3ap::save_point_detail
