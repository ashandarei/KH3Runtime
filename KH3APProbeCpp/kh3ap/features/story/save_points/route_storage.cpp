#include "kh3ap/features/story/save_points/internal.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/bridge/utilities.h"
#include "kh3ap/features/grants/grant_internal_call_depth.h"
#include <UnrealDef.hpp>
#include <Unreal/FProperty.hpp>
#include <Unreal/UClass.hpp>
#include <Unreal/UFunction.hpp>
#include <Unreal/UObject.hpp>
#include <Unreal/UObjectGlobals.hpp>
#include <DynamicOutput/DynamicOutput.hpp>
#include <algorithm>
#include <chrono>
#include <cstddef>
#include <format>
#include <string>
#include <vector>

namespace kh3ap::save_point_detail
{
    using namespace RC;
    using namespace RC::Unreal;
    using save_point_native::try_read_object_return_value;
    using save_point_native::try_resolve_cached_function;
    using save_point_native::try_write_class_function_param;
    using save_point_native::try_write_object_function_param;

    auto try_resolve_goa_save_point_data_class(const RuntimeSavePointSpawnDeps& deps,
                                               std::wstring* resolution_source) -> UClass*
    {
        if (!deps.goa_save_point_data_class_path.empty())
        {
            UClass* const direct_class = UObjectGlobals::StaticFindObject<UClass*>(
                nullptr, nullptr, std::wstring{deps.goa_save_point_data_class_path}.c_str());
            if (direct_class && kh3ap::is_uobject_valid_fast(direct_class))
            {
                if (resolution_source)
                {
                    *resolution_source = STR("class_path");
                }
                return direct_class;
            }
        }

        if (deps.goa_save_point_data_blueprint_path.empty())
        {
            return nullptr;
        }
        UObject* const blueprint_object = UObjectGlobals::StaticFindObject<UObject*>(
            nullptr, nullptr, std::wstring{deps.goa_save_point_data_blueprint_path}.c_str());
        if (!blueprint_object || !kh3ap::is_uobject_valid_fast(blueprint_object))
        {
            return nullptr;
        }
        UClass** const generated_class =
            blueprint_object->GetValuePtrByPropertyName<UClass*>(STR("GeneratedClass"));
        if (generated_class && *generated_class && kh3ap::is_uobject_valid_fast(*generated_class))
        {
            if (resolution_source)
            {
                *resolution_source = STR("blueprint_generated_class");
            }
            return *generated_class;
        }
        return nullptr;
    }

    auto try_load_goa_save_point_save_game(const RuntimeSavePointSpawnDeps& deps,
                                           std::wstring_view slot_name,
                                           std::wstring* failure_reason) -> UObject*
    {
        UFunction* const function_object =
            try_resolve_cached_function(deps.load_game_from_slot_path);
        UObject* const self_object = try_get_gameplay_statics_default_object(
            deps.gameplay_statics_default_object_path, function_object);
        if (!function_object || !self_object)
        {
            if (failure_reason)
            {
                *failure_reason = STR("LoadGameFromSlot function/default object unavailable");
            }
            return nullptr;
        }

        std::vector<std::byte> params(static_cast<size_t>(function_object->GetParmsSize()));
        std::fill(params.begin(), params.end(), std::byte{0});
        const bool wrote_slot = write_name_or_string_function_param(
            find_function_param_by_name(function_object, STR("SlotName")), params.data(),
            slot_name);
        const bool wrote_user = write_integral_function_param(
            find_function_param_by_name(function_object, STR("UserIndex")), params.data(), 0);
        if (!wrote_slot || !wrote_user)
        {
            if (failure_reason)
            {
                *failure_reason = std::format(
                    STR("LoadGameFromSlot param write failed slot={} user={}"),
                    wrote_slot ? STR("ok") : STR("failed"), wrote_user ? STR("ok") : STR("failed"));
            }
            return nullptr;
        }

        {
            const RuntimeGrantInternalCallDepth::Scope internal_call_scope{};
            self_object->ProcessEvent(function_object, params.data());
        }
        return try_read_object_return_value(function_object, params.data());
    }

    auto try_create_goa_save_point_save_game(const RuntimeSavePointSpawnDeps& deps,
                                             std::wstring* class_resolution_source,
                                             std::wstring* failure_reason) -> UObject*
    {
        UClass* const save_class =
            try_resolve_goa_save_point_data_class(deps, class_resolution_source);
        UFunction* const function_object =
            try_resolve_cached_function(deps.create_save_game_object_path);
        UObject* const self_object = try_get_gameplay_statics_default_object(
            deps.gameplay_statics_default_object_path, function_object);
        if (!save_class || !function_object || !self_object)
        {
            if (failure_reason)
            {
                *failure_reason = std::format(
                    STR("CreateSaveGameObject prereq unavailable class={} function={} self={}"),
                    format_object_debug_name(save_class), format_object_debug_name(function_object),
                    format_object_debug_name(self_object));
            }
            return nullptr;
        }

        std::vector<std::byte> params(static_cast<size_t>(function_object->GetParmsSize()));
        std::fill(params.begin(), params.end(), std::byte{0});
        if (!try_write_class_function_param(
                find_function_param_by_name(function_object, STR("SaveGameClass")), params.data(),
                save_class))
        {
            if (failure_reason)
            {
                *failure_reason = STR("CreateSaveGameObject SaveGameClass param missing");
            }
            return nullptr;
        }

        {
            const RuntimeGrantInternalCallDepth::Scope internal_call_scope{};
            self_object->ProcessEvent(function_object, params.data());
        }
        return try_read_object_return_value(function_object, params.data());
    }

    auto try_save_goa_save_point_save_game(const RuntimeSavePointSpawnDeps& deps,
                                           UObject* save_game_object, std::wstring_view slot_name,
                                           std::wstring* failure_reason) -> bool
    {
        UFunction* const function_object = try_resolve_cached_function(deps.save_game_to_slot_path);
        UObject* const self_object = try_get_gameplay_statics_default_object(
            deps.gameplay_statics_default_object_path, function_object);
        // The load/create helper validated this save object immediately
        // before this synchronous call. Avoid another GUObjectArray scan.
        if (!save_game_object || !function_object || !self_object)
        {
            if (failure_reason)
            {
                *failure_reason = STR("SaveGameToSlot function/default/save object unavailable");
            }
            return false;
        }

        std::vector<std::byte> params(static_cast<size_t>(function_object->GetParmsSize()));
        std::fill(params.begin(), params.end(), std::byte{0});
        const bool wrote_object = try_write_object_function_param(
            find_function_param_by_name(function_object, STR("SaveGameObject")), params.data(),
            save_game_object);
        const bool wrote_slot = write_name_or_string_function_param(
            find_function_param_by_name(function_object, STR("SlotName")), params.data(),
            slot_name);
        const bool wrote_user = write_integral_function_param(
            find_function_param_by_name(function_object, STR("UserIndex")), params.data(), 0);
        if (!wrote_object || !wrote_slot || !wrote_user)
        {
            if (failure_reason)
            {
                *failure_reason = std::format(
                    STR("SaveGameToSlot param write failed object={} slot={} user={}"),
                    wrote_object ? STR("ok") : STR("failed"),
                    wrote_slot ? STR("ok") : STR("failed"), wrote_user ? STR("ok") : STR("failed"));
            }
            return false;
        }

        {
            const RuntimeGrantInternalCallDepth::Scope internal_call_scope{};
            self_object->ProcessEvent(function_object, params.data());
        }
        const bool saved = try_read_bool_return_value(function_object, params.data(), true);
        if (!saved && failure_reason)
        {
            *failure_reason = STR("SaveGameToSlot returned false");
        }
        return saved;
    }

    auto sync_goa_save_point_save_game(const RuntimeSavePointSpawnDeps& deps,
                                       const SavePointSpawnRequest& request,
                                       GoASavePointSyncSummary* summary) -> bool
    {
        const auto started_at = std::chrono::steady_clock::now();
        if (!summary)
        {
            return false;
        }

        summary->requested = request.sync_goa_save_point_slot;
        summary->slot_name = request.goa_save_point_slot_name;
        summary->level_path = request.goa_save_point_level_path;
        summary->player_start = request.goa_save_point_player_start;
        if (!request.sync_goa_save_point_slot)
        {
            summary->status = STR("skipped");
            summary->reason = STR("not_requested");
            return false;
        }
        if (request.goa_save_point_slot_name.empty() ||
            request.goa_save_point_level_path_property.empty() ||
            request.goa_save_point_player_start_property.empty() ||
            request.goa_save_point_level_path.empty() ||
            request.goa_save_point_player_start.empty())
        {
            summary->status = STR("failed");
            summary->reason = STR("missing_sync_config");
            return false;
        }

        std::wstring load_failure{};
        UObject* save_game_object = try_load_goa_save_point_save_game(
            deps, request.goa_save_point_slot_name, &load_failure);
        if (save_game_object)
        {
            summary->loaded = true;
        }
        else
        {
            if (!load_failure.empty())
            {
                summary->status = STR("failed");
                summary->reason = load_failure;
                return false;
            }

            std::wstring create_failure{};
            save_game_object = try_create_goa_save_point_save_game(
                deps, &summary->class_resolution_source, &create_failure);
            if (!save_game_object)
            {
                summary->status = STR("failed");
                summary->reason =
                    create_failure.empty()
                        ? std::wstring{STR("LoadGameFromSlot returned null and create failed")}
                        : create_failure;
                return false;
            }
            summary->created = true;
        }
        const auto load_completed_at = std::chrono::steady_clock::now();

        summary->save_game_object = std::format(STR("{} @0x{:x}"), save_game_object->GetFullName(),
                                                reinterpret_cast<uintptr_t>(save_game_object));
        summary->property_writes.push_back(write_name_metadata_property(
            save_game_object, STR("goa_save_point_sync"),
            request.goa_save_point_level_path_property, request.goa_save_point_level_path));
        summary->property_writes.push_back(write_name_metadata_property(
            save_game_object, STR("goa_save_point_sync"),
            request.goa_save_point_player_start_property, request.goa_save_point_player_start));

        const bool wrote_properties =
            std::all_of(summary->property_writes.begin(), summary->property_writes.end(),
                        [](const MetadataWriteSummary& write)
                        {
                            return write.wrote;
                        });
        if (!wrote_properties)
        {
            summary->status = STR("failed");
            summary->reason = STR("property_write_failed");
            return false;
        }
        const auto properties_completed_at = std::chrono::steady_clock::now();

        const bool properties_changed =
            std::any_of(summary->property_writes.begin(), summary->property_writes.end(),
                        [](const MetadataWriteSummary& write)
                        {
                            return write.status == STR("written");
                        });
        if (summary->loaded && !summary->created && !properties_changed)
        {
            summary->status = STR("already_current");
            summary->reason = STR("route_unchanged");
            const auto completed_at = std::chrono::steady_clock::now();
            const auto elapsed_us = [](auto begin, auto end) -> uint64_t
            {
                return static_cast<uint64_t>(
                    std::chrono::duration_cast<std::chrono::microseconds>(end - begin).count());
            };
            Output::send<LogLevel::Verbose>(
                STR("[KH3APProbeCpp] RuntimeGoASavePointSync status={} total_us={} load_us={} "
                    "properties_us={} save_us=0 saved=false\n"),
                summary->status, elapsed_us(started_at, completed_at),
                elapsed_us(started_at, load_completed_at),
                elapsed_us(load_completed_at, properties_completed_at));
            return true;
        }

        std::wstring save_failure{};
        summary->saved = try_save_goa_save_point_save_game(
            deps, save_game_object, request.goa_save_point_slot_name, &save_failure);
        if (!summary->saved)
        {
            summary->status = STR("failed");
            summary->reason =
                save_failure.empty() ? std::wstring{STR("save_failed")} : save_failure;
            return false;
        }

        summary->status = STR("saved");
        const auto completed_at = std::chrono::steady_clock::now();
        const auto elapsed_us = [](auto begin, auto end) -> uint64_t
        {
            return static_cast<uint64_t>(
                std::chrono::duration_cast<std::chrono::microseconds>(end - begin).count());
        };
        Output::send<LogLevel::Verbose>(
            STR("[KH3APProbeCpp] RuntimeGoASavePointSync status={} total_us={} load_us={} "
                "properties_us={} save_us={} saved=true\n"),
            summary->status, elapsed_us(started_at, completed_at),
            elapsed_us(started_at, load_completed_at),
            elapsed_us(load_completed_at, properties_completed_at),
            elapsed_us(properties_completed_at, completed_at));
        return true;
    }
} // namespace kh3ap::save_point_detail
