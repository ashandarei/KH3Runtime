#include "kh3ap/features/story/save_points/internal.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/bridge/utilities.h"
#include <UnrealDef.hpp>
#include <Unreal/FProperty.hpp>
#include <Unreal/UClass.hpp>
#include <Unreal/UFunction.hpp>
#include <Unreal/UObject.hpp>
#include <Unreal/UObjectGlobals.hpp>
#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <iomanip>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace kh3ap::save_point_detail
{
    using namespace RC;
    using namespace RC::Unreal;
    using save_point_native::describe_object_property;
    using save_point_text::append_json_optional_bool_field;
    using save_point_text::append_json_string_field;

    namespace
    {
        constexpr std::array<std::chrono::milliseconds, 5> s_lifetime_sample_offsets{
            std::chrono::milliseconds{250},   std::chrono::milliseconds{1000},
            std::chrono::milliseconds{3000},  std::chrono::milliseconds{10000},
            std::chrono::milliseconds{30000},
        };
        constexpr size_t s_lifetime_samples_per_poll = 1;

        std::vector<SavePointSpawnWatch> s_save_point_spawn_watches{};

    } // namespace
    auto append_lifetime_sample(const RuntimeSavePointSpawnDeps& deps,
                                const SavePointSpawnWatch& watch, size_t sample_index,
                                std::chrono::milliseconds target_offset,
                                std::chrono::milliseconds elapsed) -> void
    {
        if (!deps.bridge_dir.has_value())
        {
            return;
        }

        UObject* const actor = watch.actor;
        const bool actor_is_real = actor && kh3ap::is_uobject_valid_fast(actor);
        UObject* const runtime_player_start = watch.runtime_player_start;
        const bool runtime_player_start_is_real =
            runtime_player_start && kh3ap::is_uobject_valid_fast(runtime_player_start);
        std::ostringstream json;
        json << "{";
        json << "\"schema\":\"kh3ap.runtime_save_point_spawn_lifetime_sample.v1\"";
        json << ",\"protocol_name\":\"" << json_escape(deps.protocol_name) << "\"";
        json << ",\"protocol_version\":" << deps.protocol_version;
        json << ",\"event\":\"runtime_save_point_spawn_lifetime_sample\"";
        append_json_string_field(json, "source", watch.source);
        append_json_string_field(json, "request_id", watch.request_id);
        const std::string observed_at = now_iso8601_utc();
        append_json_string_field(json, "observed_at_utc",
                                 std::wstring{observed_at.begin(), observed_at.end()});
        append_json_string_field(json, "build_stamp", deps.build_stamp);
        json << ",\"sample_index\":" << sample_index;
        json << ",\"target_ms\":" << target_offset.count();
        json << ",\"elapsed_ms\":" << elapsed.count();
        json << ",\"actor_address\":\"0x" << std::hex << reinterpret_cast<uintptr_t>(actor)
             << std::dec << "\"";
        append_json_string_field(json, "spawned_actor_initial", watch.spawned_actor);
        append_json_string_field(json, "actor", format_object_debug_name(actor));
        json << ",\"actor_is_real\":" << (actor_is_real ? "true" : "false");
        json << ",\"runtime_player_start_address\":\"0x" << std::hex
             << reinterpret_cast<uintptr_t>(runtime_player_start) << std::dec << "\"";
        append_json_string_field(json, "spawned_runtime_player_start_initial",
                                 watch.spawned_runtime_player_start);
        append_json_string_field(json, "runtime_player_start",
                                 format_object_debug_name(runtime_player_start));
        json << ",\"runtime_player_start_is_real\":"
             << (runtime_player_start_is_real ? "true" : "false");
        json << ",\"deep_state\":" << (deps.enable_lifetime_deep_samples ? "true" : "false");

        if (actor_is_real && deps.enable_lifetime_deep_samples)
        {
            append_json_optional_bool_field(
                json, "is_actor_being_destroyed",
                try_call_bool_return_function(actor,
                                              STR("/Script/Engine.Actor:IsActorBeingDestroyed")));
            append_json_optional_bool_field(
                json, "bp_is_gimmick_active",
                try_call_bool_return_function(
                    actor, STR("/Script/TresGame.TresSavePointActor:BP_IsGimmickActive")));
            json << ",\"properties\":{";
            bool first_property = true;
            append_property_snapshot_field(json, actor, STR("m_bAutoActivate"), &first_property);
            append_property_snapshot_field(json, actor, STR("m_bGimmickComponentAutoLinkActivate"),
                                           &first_property);
            append_property_snapshot_field(json, actor, STR("m_bIsActive"), &first_property);
            append_property_snapshot_field(json, actor, STR("m_bIsAccessed"), &first_property);
            append_property_snapshot_field(json, actor, STR("m_bLimitedSavePoint"),
                                           &first_property);
            append_property_snapshot_field(json, actor, STR("m_IsAutoSave"), &first_property);
            append_property_snapshot_field(json, actor, STR("m_IsEnableNavMap"), &first_property);
            append_property_snapshot_field(json, actor, STR("m_DispMapName"), &first_property);
            append_property_snapshot_field(json, actor, STR("m_NavMapDataTableKey"),
                                           &first_property);
            append_property_snapshot_field(json, actor, STR("PlayerStartTag"), &first_property);
            json << "}";
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
        if (runtime_player_start_is_real && deps.enable_lifetime_deep_samples)
        {
            json << ",\"runtime_player_start_properties\":{";
            bool first_property = true;
            append_property_snapshot_field(json, runtime_player_start, STR("PlayerStartTag"),
                                           &first_property);
            append_property_snapshot_field(json, runtime_player_start, STR("m_DispMapName"),
                                           &first_property);
            append_property_snapshot_field(json, runtime_player_start, STR("m_IsAutoSave"),
                                           &first_property);
            append_property_snapshot_field(json, runtime_player_start, STR("m_IsEnableNavMap"),
                                           &first_property);
            append_property_snapshot_field(json, runtime_player_start, STR("m_Enabled"),
                                           &first_property);
            json << "}";
        }
        json << "}\n";

        append_text_file(*deps.bridge_dir /
                             std::filesystem::path{std::wstring{deps.events_filename}},
                         json.str());
    }

    auto poll_save_point_spawn_watches(const RuntimeSavePointSpawnDeps& deps) -> void
    {
        if (!deps.bridge_dir.has_value() || s_save_point_spawn_watches.empty())
        {
            return;
        }

        const auto now = std::chrono::steady_clock::now();
        size_t emitted_samples = 0;
        for (SavePointSpawnWatch& watch : s_save_point_spawn_watches)
        {
            const auto elapsed =
                std::chrono::duration_cast<std::chrono::milliseconds>(now - watch.spawned_at);
            while (watch.next_sample_index < s_lifetime_sample_offsets.size() &&
                   elapsed >= s_lifetime_sample_offsets[watch.next_sample_index])
            {
                append_lifetime_sample(deps, watch, watch.next_sample_index,
                                       s_lifetime_sample_offsets[watch.next_sample_index], elapsed);
                ++watch.next_sample_index;
                ++emitted_samples;
                if (emitted_samples >= s_lifetime_samples_per_poll)
                {
                    break;
                }
            }
            if (emitted_samples >= s_lifetime_samples_per_poll)
            {
                break;
            }
        }

        s_save_point_spawn_watches.erase(
            std::remove_if(s_save_point_spawn_watches.begin(), s_save_point_spawn_watches.end(),
                           [](const SavePointSpawnWatch& watch)
                           {
                               return watch.next_sample_index >= s_lifetime_sample_offsets.size();
                           }),
            s_save_point_spawn_watches.end());
    }
    auto register_spawn_watch(SavePointSpawnWatch watch) -> void
    {
        s_save_point_spawn_watches.push_back(std::move(watch));
    }

} // namespace kh3ap::save_point_detail
