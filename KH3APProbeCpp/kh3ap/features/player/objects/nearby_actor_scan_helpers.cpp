#include "kh3ap/features/player/objects/nearby_actor_scan_helpers.h"

#include "kh3ap/bridge/transport/bridge_paths.h"
#include "kh3ap/bridge/serialization/json_fields.h"
#include "kh3ap/bridge/transport/bridge_io_helpers.h"
#include "kh3ap/features/player/context/context_helpers.h"
#include "kh3ap/features/grants/grant_internal_call_depth.h"
#include "kh3ap/features/grants/grant_state.h"
#include "kh3ap/features/player/objects/object_resolver.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/app/state.h"
#include "kh3ap/bridge/utilities.h"

#include <DynamicOutput/DynamicOutput.hpp>
#include <UnrealDef.hpp>
#include <Unreal/FProperty.hpp>
#include <Unreal/Property/FBoolProperty.hpp>
#include <Unreal/UFunction.hpp>
#include <Unreal/UObject.hpp>
#include <Unreal/UObjectGlobals.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <cwctype>
#include <format>
#include <iomanip>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace kh3ap
{
    namespace
    {
        constexpr auto max_actor_name_probe_index = 500000;
        constexpr double default_scan_radius = 5000.0;
        constexpr double maximum_scan_radius = 100000.0;
        constexpr int default_scan_result_limit = 80;
        constexpr int maximum_scan_result_limit = 200;
        constexpr int default_scan_candidate_limit = 160;
        constexpr int maximum_scan_candidate_limit = 500;
        constexpr int maximum_name_scan_candidate_limit = 200;
        constexpr int coordinate_decimal_places = 3;
    }

    using namespace RC;
    using namespace RC::Unreal;

    namespace
    {
        struct ScanVector
        {
            double x{};
            double y{};
            double z{};
        };

        struct NearbyActorScanRequest
        {
            std::wstring request_id{};
            double radius{default_scan_radius};
            int max_results{default_scan_result_limit};
            int max_candidates{default_scan_candidate_limit};
            int max_name_scan_candidates{};
            bool include_name_scan{};
            bool require_sora_pawn{true};
            bool continuous{};
            std::vector<std::wstring> class_names{};
            std::vector<std::wstring> name_filters{};
        };

        struct CandidateActor
        {
            UObject* object{};
            std::wstring source{};
        };

        struct NearbyActorEntry
        {
            UObject* object{};
            std::wstring source{};
            std::wstring object_name{};
            std::wstring class_name{};
            std::wstring outer_chain{};
            std::wstring root_component{};
            std::wstring actor_label{};
            std::wstring player_start_tag{};
            std::wstring disp_map_name{};
            std::wstring is_auto_save{};
            std::wstring enabled{};
            bool is_game_mode_auto_save_point{};
            bool is_game_instance_teleport_start{};
            bool location_available{};
            bool distance_from_pawn_available{};
            ScanVector location{};
            double distance{};
        };

        auto append_json_string_field(std::ostringstream& json, std::string_view key, std::wstring_view value) -> void
        {
            json << ",\"" << key << "\":\"" << json_escape(value) << "\"";
        }

        auto append_json_number_field(std::ostringstream& json, std::string_view key, double value) -> void
        {
            json << ",\"" << key << "\":" << std::fixed << std::setprecision(coordinate_decimal_places) << value;
        }

        auto append_unique(std::vector<std::wstring>& values, std::wstring value) -> void
        {
            if (value.empty())
            {
                return;
            }
            if (std::find(values.begin(), values.end(), value) == values.end())
            {
                values.push_back(std::move(value));
            }
        }

        auto default_class_names() -> std::vector<std::wstring>
        {
            return {
                    STR("TresPlayerStart"),
                    STR("PlayerStart"),
                    STR("TresSavePointActor"),
                    STR("g_ex_SavePoint_C"),
                    STR("g_ex_SavePoint"),
            };
        }

        auto default_name_filters() -> std::vector<std::wstring>
        {
            return {
                    STR("PlayerStart"),
                    STR("Start"),
                    STR("Spawn"),
                    STR("Save"),
                    STR("Auto"),
                    STR("Check"),
                    STR("Checkpoint"),
                    STR("Restart"),
                    STR("Portal"),
                    STR("Teleport"),
            };
        }

        auto clamped_int_field(
                const std::string& text,
                std::string_view field_name,
                int default_value,
                int min_value,
                int max_value) -> int
        {
            const auto raw = JsonFieldReader::int64_field(text, field_name);
            if (!raw.has_value())
            {
                return default_value;
            }
            return std::clamp(static_cast<int>(*raw), min_value, max_value);
        }

        auto clamped_number_field(
                const std::string& text,
                std::string_view field_name,
                double default_value,
                double min_value,
                double max_value) -> double
        {
            const auto raw = JsonFieldReader::number_field(text, field_name);
            if (!raw.has_value())
            {
                return default_value;
            }
            return std::clamp(*raw, min_value, max_value);
        }

        auto parse_request(const std::string& text) -> NearbyActorScanRequest
        {
            NearbyActorScanRequest request{};
            request.request_id = JsonFieldReader::string_field(text, "request_id").value_or(std::wstring{});
            request.radius = clamped_number_field(text, "radius", default_scan_radius, 0.0, maximum_scan_radius);
            request.max_results = clamped_int_field(text, "max_results", default_scan_result_limit, 1, maximum_scan_result_limit);
            request.max_candidates = clamped_int_field(text, "max_candidates", default_scan_candidate_limit, 1, maximum_scan_candidate_limit);
            request.max_name_scan_candidates = clamped_int_field(text, "max_name_scan_candidates", 0, 0, maximum_name_scan_candidate_limit);
            request.include_name_scan = JsonFieldReader::bool_field(text, "include_name_scan").value_or(false);
            request.require_sora_pawn = JsonFieldReader::bool_field(text, "require_sora_pawn").value_or(true);
            request.continuous = JsonFieldReader::bool_field(text, "continuous").value_or(false);

            request.class_names = JsonFieldReader::string_array_field(text, "class_names");
            if (request.class_names.empty())
            {
                request.class_names = default_class_names();
            }

            request.name_filters = JsonFieldReader::string_array_field(text, "name_filters");
            if (request.name_filters.empty())
            {
                request.name_filters = default_name_filters();
            }
            return request;
        }

        auto try_get_actor_location(
                UObject* object,
                std::wstring_view actor_location_function_path,
                std::wstring* failure_reason) -> std::optional<ScanVector>
        {
            if (!object || !kh3ap::is_uobject_valid_fast(object))
            {
                if (failure_reason)
                {
                    *failure_reason = STR("invalid_actor");
                }
                return std::nullopt;
            }

            UFunction* const function_object =
                    UObjectGlobals::StaticFindObject<UFunction*>(nullptr, nullptr, std::wstring{actor_location_function_path}.c_str());
            if (!function_object)
            {
                if (failure_reason)
                {
                    *failure_reason = STR("missing_actor_location_function");
                }
                return std::nullopt;
            }
            FProperty* const return_property = find_function_param_by_name(function_object, STR("ReturnValue"));
            if (!return_property)
            {
                if (failure_reason)
                {
                    *failure_reason = STR("missing_actor_location_return_value");
                }
                return std::nullopt;
            }

            std::vector<std::byte> params(static_cast<size_t>(function_object->GetParmsSize()));
            std::fill(params.begin(), params.end(), std::byte{0});
            {
                const RuntimeGrantInternalCallDepth::Scope internal_call_scope{};
                object->ProcessEvent(function_object, params.data());
            }
            void* const value_ptr = return_property->ContainerPtrToValuePtr<void>(params.data());
            const int element_size = return_property->GetElementSize();
            if (!value_ptr || element_size < static_cast<int>(sizeof(float) * 3))
            {
                if (failure_reason)
                {
                    *failure_reason = std::format(STR("invalid_actor_location_return_size:{}"), element_size);
                }
                return std::nullopt;
            }
            if (element_size >= static_cast<int>(sizeof(double) * 3))
            {
                double values[3]{};
                std::memcpy(values, value_ptr, sizeof(values));
                return ScanVector{values[0], values[1], values[2]};
            }

            float values[3]{};
            std::memcpy(values, value_ptr, sizeof(values));
            return ScanVector{
                    static_cast<double>(values[0]),
                    static_cast<double>(values[1]),
                    static_cast<double>(values[2])};
        }

        auto distance_between(const ScanVector& left, const ScanVector& right) -> double
        {
            const double dx = left.x - right.x;
            const double dy = left.y - right.y;
            const double dz = left.z - right.z;
            return std::sqrt(dx * dx + dy * dy + dz * dz);
        }

        auto safe_class_name(UObject* object) -> std::wstring
        {
            if (!object || !kh3ap::is_uobject_valid_fast(object))
            {
                return STR("<invalid>");
            }
            UClass* const object_class = object->GetClassPrivate();
            return object_class ? object_class->GetName() : std::wstring{};
        }

        auto read_unsigned_property_value(const void* value_ptr, int element_size) -> uint64_t
        {
            if (!value_ptr || element_size <= 0)
            {
                return 0;
            }
            const int clamped_size = std::min(element_size, static_cast<int>(sizeof(uint64_t)));
            uint64_t value = 0;
            std::memcpy(&value, value_ptr, static_cast<size_t>(clamped_size));
            return value;
        }

        auto try_decode_fname_property_value(UObject* object, std::wstring_view property_name) -> std::optional<std::wstring>
        {
            if (!object || !kh3ap::is_uobject_valid_fast(object))
            {
                return std::nullopt;
            }

            const std::wstring owned_property_name{property_name};
            FProperty* const property = object->GetPropertyByNameInChain(owned_property_name.c_str());
            void* const value_ptr = object->GetValuePtrByPropertyNameInChain(owned_property_name.c_str());
            if (!property || !value_ptr)
            {
                return std::nullopt;
            }

            const std::wstring property_class = property->GetClass().GetName();
            const int element_size = property->GetElementSize();
            if (property_class == STR("NameProperty") && element_size >= static_cast<int>(sizeof(FName)))
            {
                const auto* const name_value = static_cast<const FName*>(value_ptr);
                const std::wstring direct_name = name_value ? name_value->ToString() : std::wstring{};
                if (!direct_name.empty() && direct_name != STR("None"))
                {
                    return direct_name;
                }
            }

            if (element_size < static_cast<int>(sizeof(uint32_t)))
            {
                return std::nullopt;
            }

            const uint64_t raw_u64 = read_unsigned_property_value(value_ptr, element_size);
            const auto low32 = static_cast<uint32_t>(raw_u64 & 0xffffffffull);
            const auto high32 = static_cast<uint32_t>((raw_u64 >> 32) & 0xffffffffull);
            if (low32 == 0 || low32 >= max_actor_name_probe_index)
            {
                return std::nullopt;
            }

#ifdef WITH_CASE_PRESERVING_NAME
            const FName pair_name{low32, high32, 0};
            const std::wstring pair_text = pair_name.ToString();
            if (!pair_text.empty() && pair_text != STR("None"))
            {
                return pair_text;
            }
#endif
            const FName first_name{low32, 0};
            const std::wstring first_text = first_name.ToString();
            if (!first_text.empty() && first_text != STR("None"))
            {
                return first_text;
            }
            return std::nullopt;
        }

        auto scalar_property_or_empty(UObject* object, std::wstring_view property_name) -> std::wstring
        {
            const std::wstring owned_property_name{property_name};
            if (property_name == STR("PlayerStartTag"))
            {
                if (const auto decoded = try_decode_fname_property_value(object, property_name); decoded.has_value())
                {
                    return *decoded;
                }
            }
            return try_read_object_scalar_property_as_string(object, owned_property_name.c_str()).value_or(std::wstring{});
        }

        auto bool_property_or_empty(UObject* object, std::wstring_view property_name) -> std::wstring
        {
            const std::wstring owned_property_name{property_name};
            const auto value = try_read_object_bool_property(object, owned_property_name.c_str());
            if (!value.has_value())
            {
                return std::wstring{};
            }
            return *value ? std::wstring{STR("true")} : std::wstring{STR("false")};
        }

        auto object_ref_or_empty(UObject* object, std::wstring_view property_name) -> UObject*
        {
            const std::wstring owned_property_name{property_name};
            return try_read_object_property_object(object, owned_property_name.c_str());
        }

        auto append_candidate(std::vector<CandidateActor>& candidates, UObject* object, std::wstring_view source) -> void
        {
            if (!object || !kh3ap::is_uobject_valid_fast(object))
            {
                return;
            }
            if (object->GetFullName().find(STR("Default__")) != std::wstring::npos)
            {
                return;
            }
            for (CandidateActor& candidate : candidates)
            {
                if (candidate.object == object)
                {
                    if (!source.empty() && candidate.source.find(source) == std::wstring::npos)
                    {
                        if (!candidate.source.empty())
                        {
                            candidate.source += STR(";");
                        }
                        candidate.source += source;
                    }
                    return;
                }
            }
            candidates.push_back(CandidateActor{object, std::wstring{source}});
        }

        auto collect_reference_objects(
                std::vector<UObject*>& auto_save_points,
                std::vector<UObject*>& teleport_starts) -> void
        {
            std::vector<UObject*> game_modes;
            std::vector<UObject*> game_instances;
            UObjectGlobals::FindAllOf(STR("TresGameMode"), game_modes);
            UObjectGlobals::FindAllOf(STR("TresGameInstance"), game_instances);

            for (UObject* const object : game_modes)
            {
                if (!object || !kh3ap::is_uobject_valid_fast(object))
                {
                    continue;
                }
                if (UObject* const auto_save_point = object_ref_or_empty(object, STR("m_AutoSavePoint"));
                    auto_save_point && kh3ap::is_uobject_valid_fast(auto_save_point))
                {
                    auto_save_points.push_back(auto_save_point);
                }
            }

            for (UObject* const object : game_instances)
            {
                if (!object || !kh3ap::is_uobject_valid_fast(object))
                {
                    continue;
                }
                if (UObject* const teleport_start = object_ref_or_empty(object, STR("m_pPlayerStartForTeleport"));
                    teleport_start && kh3ap::is_uobject_valid_fast(teleport_start))
                {
                    teleport_starts.push_back(teleport_start);
                }
            }
        }

        auto vector_contains_object(const std::vector<UObject*>& objects, UObject* object) -> bool
        {
            return std::find(objects.begin(), objects.end(), object) != objects.end();
        }

        auto collect_candidates(const NearbyActorScanRequest& request) -> std::vector<CandidateActor>
        {
            std::vector<CandidateActor> candidates{};
            for (const std::wstring& class_name : request.class_names)
            {
                if (static_cast<int>(candidates.size()) >= request.max_candidates)
                {
                    break;
                }
                std::vector<UObject*> objects;
                UObjectGlobals::FindAllOf(class_name, objects);
                for (UObject* const object : objects)
                {
                    append_candidate(candidates, object, std::wstring{STR("class:")} + class_name);
                    if (static_cast<int>(candidates.size()) >= request.max_candidates)
                    {
                        break;
                    }
                }
            }

            return candidates;
        }

        auto make_entry(
                const CandidateActor& candidate,
                const ScanVector& location,
                bool location_available,
                double distance,
                bool distance_from_pawn_available,
                const std::vector<UObject*>& auto_save_points,
                const std::vector<UObject*>& teleport_starts) -> NearbyActorEntry
        {
            UObject* const object = candidate.object;
            NearbyActorEntry entry{};
            entry.object = object;
            entry.source = candidate.source;
            entry.object_name = format_object_debug_name(object);
            entry.class_name = safe_class_name(object);
            entry.outer_chain = format_object_outer_chain(object, 6);
            entry.root_component = format_object_debug_name(object_ref_or_empty(object, STR("RootComponent")));
            entry.actor_label = scalar_property_or_empty(object, STR("ActorLabel"));
            entry.player_start_tag = scalar_property_or_empty(object, STR("PlayerStartTag"));
            entry.disp_map_name = scalar_property_or_empty(object, STR("m_DispMapName"));
            entry.is_auto_save = bool_property_or_empty(object, STR("m_IsAutoSave"));
            entry.enabled = bool_property_or_empty(object, STR("m_Enabled"));
            entry.is_game_mode_auto_save_point = vector_contains_object(auto_save_points, object);
            entry.is_game_instance_teleport_start = vector_contains_object(teleport_starts, object);
            entry.location_available = location_available;
            entry.distance_from_pawn_available = distance_from_pawn_available;
            entry.location = location;
            entry.distance = distance;
            return entry;
        }

        auto write_result(
                const RuntimeNearbyActorScanDeps& deps,
                const NearbyActorScanRequest& request,
                std::wstring_view status,
                std::wstring_view reason,
                UObject* pawn,
                const std::optional<ScanVector>& pawn_location,
                int candidate_count,
                int located_count,
                int location_failed_count,
                std::wstring_view first_location_failure,
                const std::vector<NearbyActorEntry>& entries) -> void
        {
            if (!deps.bridge_dir.has_value())
            {
                return;
            }

            std::ostringstream json;
            json << "{";
            json << "\"schema\":\"kh3ap.runtime_nearby_actor_scan_result.v1\"";
            json << ",\"protocol_name\":\"" << json_escape(deps.protocol_name) << "\"";
            json << ",\"protocol_version\":" << deps.protocol_version;
            json << ",\"event\":\"runtime_nearby_actor_scan_result\"";
            append_json_string_field(json, "status", status);
            append_json_string_field(json, "reason", reason);
            append_json_string_field(json, "request_id", request.request_id);
            const std::string observed_at = now_iso8601_utc();
            append_json_string_field(json, "observed_at_utc", std::wstring{observed_at.begin(), observed_at.end()});
            append_json_string_field(json, "build_stamp", deps.build_stamp);
            append_json_string_field(json, "source", STR("runtime_nearby_actor_scan"));
            append_json_string_field(
                    json,
                    "scan_mode",
                    request.require_sora_pawn ? std::wstring_view{STR("nearby_sora")}
                                              : std::wstring_view{STR("loaded_class_instances")});
            json << ",\"require_sora_pawn\":" << (request.require_sora_pawn ? "true" : "false");
            json << ",\"continuous\":" << (request.continuous ? "true" : "false");
            append_json_string_field(json, "pawn", format_object_debug_name(pawn));
            if (pawn_location.has_value())
            {
                json << ",\"pawn_location\":{";
                json << "\"x\":" << std::fixed << std::setprecision(3) << pawn_location->x;
                json << ",\"y\":" << std::fixed << std::setprecision(3) << pawn_location->y;
                json << ",\"z\":" << std::fixed << std::setprecision(3) << pawn_location->z;
                json << "}";
            }
            append_json_number_field(json, "radius", request.radius);
            json << ",\"max_results\":" << request.max_results;
            json << ",\"max_candidates\":" << request.max_candidates;
            json << ",\"name_scan_requested\":" << (request.include_name_scan ? "true" : "false");
            json << ",\"name_scan_performed\":false";
            json << ",\"candidate_count\":" << candidate_count;
            json << ",\"located_count\":" << located_count;
            json << ",\"location_failed_count\":" << location_failed_count;
            append_json_string_field(json, "first_location_failure", first_location_failure);
            json << ",\"match_count\":" << entries.size();
            json << ",\"class_names\":[";
            for (size_t index = 0; index < request.class_names.size(); ++index)
            {
                if (index > 0)
                {
                    json << ",";
                }
                json << "\"" << json_escape(request.class_names[index]) << "\"";
            }
            json << "]";
            json << ",\"name_filters\":[";
            for (size_t index = 0; index < request.name_filters.size(); ++index)
            {
                if (index > 0)
                {
                    json << ",";
                }
                json << "\"" << json_escape(request.name_filters[index]) << "\"";
            }
            json << "]";
            json << ",\"runtime_context\":\"" << json_escape(runtime_context_summary(deps.runtime_state, deps.bridge_dir)) << "\"";
            json << ",\"actors\":[";
            const size_t emit_count = std::min(entries.size(), static_cast<size_t>(request.max_results));
            for (size_t index = 0; index < emit_count; ++index)
            {
                const NearbyActorEntry& entry = entries[index];
                if (index > 0)
                {
                    json << ",";
                }
                json << "{";
                json << "\"rank\":" << index;
                json << ",\"distance_from_pawn_available\":"
                     << (entry.distance_from_pawn_available ? "true" : "false");
                if (entry.distance_from_pawn_available)
                {
                    json << ",\"distance\":" << std::fixed << std::setprecision(3) << entry.distance;
                }
                else
                {
                    json << ",\"distance\":null";
                }
                json << ",\"location_available\":" << (entry.location_available ? "true" : "false");
                if (entry.location_available)
                {
                    json << ",\"location\":{";
                    json << "\"x\":" << std::fixed << std::setprecision(3) << entry.location.x;
                    json << ",\"y\":" << std::fixed << std::setprecision(3) << entry.location.y;
                    json << ",\"z\":" << std::fixed << std::setprecision(3) << entry.location.z;
                    json << "}";
                }
                else
                {
                    json << ",\"location\":null";
                }
                json << ",\"object_address\":\"0x" << std::hex << reinterpret_cast<uintptr_t>(entry.object) << std::dec << "\"";
                json << ",\"object\":\"" << json_escape(entry.object_name) << "\"";
                json << ",\"class\":\"" << json_escape(entry.class_name) << "\"";
                json << ",\"outer_chain\":\"" << json_escape(entry.outer_chain) << "\"";
                json << ",\"root_component\":\"" << json_escape(entry.root_component) << "\"";
                json << ",\"source\":\"" << json_escape(entry.source) << "\"";
                json << ",\"is_game_mode_auto_save_point\":" << (entry.is_game_mode_auto_save_point ? "true" : "false");
                json << ",\"is_game_instance_teleport_start\":" << (entry.is_game_instance_teleport_start ? "true" : "false");
                json << ",\"properties\":{";
                json << "\"ActorLabel\":\"" << json_escape(entry.actor_label) << "\"";
                json << ",\"PlayerStartTag\":\"" << json_escape(entry.player_start_tag) << "\"";
                json << ",\"m_DispMapName\":\"" << json_escape(entry.disp_map_name) << "\"";
                json << ",\"m_IsAutoSave\":\"" << json_escape(entry.is_auto_save) << "\"";
                json << ",\"m_Enabled\":\"" << json_escape(entry.enabled) << "\"";
                json << "}";
                json << "}";
            }
            json << "]";
            json << "}\n";

            write_text_file(*deps.bridge_dir / std::filesystem::path{std::wstring{deps.result_filename}}, json.str());
            append_text_file(*deps.bridge_dir / std::filesystem::path{std::wstring{deps.events_filename}}, json.str());
        }
    }

    auto process_pending_runtime_nearby_actor_scan(const RuntimeNearbyActorScanDeps& deps) -> void
    {
        if (!deps.bridge_dir.has_value())
        {
            return;
        }

        const auto request_path = *deps.bridge_dir / std::filesystem::path{std::wstring{deps.request_filename}};
        if (!path_points_to_regular_file(request_path))
        {
            return;
        }
        const auto request_text = read_text_file(request_path);
        if (!request_text.has_value())
        {
            return;
        }

        NearbyActorScanRequest request = parse_request(*request_text);
        if (request.request_id.empty())
        {
            const std::string observed_at = now_iso8601_utc();
            request.request_id = std::wstring{STR("nearby-actor-scan-")} +
                                 std::wstring{observed_at.begin(), observed_at.end()};
        }

        if (request.require_sora_pawn && deps.runtime_state.grant_state().special_battle_mode_active())
        {
            write_result(
                    deps,
                    request,
                    STR("waiting"),
                    STR("special_battle_mode_active"),
                    nullptr,
                    std::nullopt,
                    0,
                    0,
                    0,
                    STR(""),
                    {});
            return;
        }

        UObject* const pawn = deps.runtime_state.grant_state().pawn_state().object();
        const bool has_safe_sora_pawn =
                pawn && kh3ap::is_uobject_valid_fast(pawn) && RuntimeObjectResolver::is_sora_ability_pawn_candidate(pawn);
        if (request.require_sora_pawn && !has_safe_sora_pawn)
        {
            write_result(
                    deps,
                    request,
                    STR("waiting"),
                    STR("missing_safe_sora_pawn"),
                    pawn,
                    std::nullopt,
                    0,
                    0,
                    0,
                    STR(""),
                    {});
            return;
        }

        std::wstring pawn_location_failure{};
        std::optional<ScanVector> pawn_location{};
        if (has_safe_sora_pawn)
        {
            pawn_location = try_get_actor_location(pawn, deps.actor_location_function_path, &pawn_location_failure);
        }
        if (request.require_sora_pawn && !pawn_location.has_value())
        {
            write_result(
                    deps,
                    request,
                    STR("failed"),
                    pawn_location_failure.empty() ? std::wstring_view{STR("missing_pawn_location")} : std::wstring_view{pawn_location_failure},
                    pawn,
                    std::nullopt,
                    0,
                    0,
                    0,
                    STR(""),
                    {});
            try_remove_file(request_path);
            return;
        }

        std::vector<UObject*> auto_save_points;
        std::vector<UObject*> teleport_starts;
        std::vector<CandidateActor> candidates = collect_candidates(request);
        if (request.require_sora_pawn)
        {
            collect_reference_objects(auto_save_points, teleport_starts);
            for (UObject* const object : auto_save_points)
            {
                append_candidate(candidates, object, STR("game_mode_auto_save_point"));
            }
            for (UObject* const object : teleport_starts)
            {
                append_candidate(candidates, object, STR("game_instance_teleport_start"));
            }
        }

        std::vector<NearbyActorEntry> entries{};
        int located_count = 0;
        int location_failed_count = 0;
        std::wstring first_location_failure{};
        for (const CandidateActor& candidate : candidates)
        {
            if (!candidate.object || !kh3ap::is_uobject_valid_fast(candidate.object))
            {
                continue;
            }
            if (!request.require_sora_pawn)
            {
                entries.push_back(make_entry(
                        candidate,
                        ScanVector{},
                        false,
                        0.0,
                        false,
                        auto_save_points,
                        teleport_starts));
                continue;
            }
            std::wstring location_failure{};
            const auto location = try_get_actor_location(candidate.object, deps.actor_location_function_path, &location_failure);
            if (!location.has_value())
            {
                ++location_failed_count;
                if (first_location_failure.empty())
                {
                    first_location_failure = format_object_debug_name(candidate.object) + STR(" -> ") + location_failure;
                }
                continue;
            }
            ++located_count;
            const bool distance_from_pawn_available = pawn_location.has_value();
            const double distance =
                    distance_from_pawn_available ? distance_between(*pawn_location, *location) : 0.0;
            if (request.require_sora_pawn && request.radius > 0.0 && distance > request.radius)
            {
                continue;
            }
            entries.push_back(make_entry(
                    candidate,
                    *location,
                    true,
                    distance,
                    distance_from_pawn_available,
                    auto_save_points,
                    teleport_starts));
        }

        std::sort(entries.begin(), entries.end(), [](const NearbyActorEntry& left, const NearbyActorEntry& right) {
            if (left.distance_from_pawn_available != right.distance_from_pawn_available)
            {
                return left.distance_from_pawn_available;
            }
            return left.distance < right.distance;
        });
        if (entries.size() > static_cast<size_t>(request.max_results))
        {
            entries.resize(static_cast<size_t>(request.max_results));
        }

        write_result(
                deps,
                request,
                request.continuous ? std::wstring_view{STR("monitoring")}
                                   : std::wstring_view{STR("completed")},
                STR(""),
                pawn,
                pawn_location,
                static_cast<int>(candidates.size()),
                located_count,
                location_failed_count,
                first_location_failure,
                entries);
        if (!request.continuous)
        {
            try_remove_file(request_path);
        }

        if (deps.debug_logging)
        {
            Output::send<LogLevel::Verbose>(
                    STR("[KH3APProbeCpp] RuntimeNearbyActorScan completed request_id={} candidates={} located={} matches={}\n"),
                    request.request_id,
                    candidates.size(),
                    located_count,
                    entries.size());
        }
    }
}
