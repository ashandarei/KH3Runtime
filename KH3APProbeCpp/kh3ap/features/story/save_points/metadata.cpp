#include "kh3ap/features/story/save_points/internal.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/bridge/utilities.h"
#include "kh3ap/bridge/serialization/json_fields.h"
#include "kh3ap/features/grants/vbonus/vbonus_actor_helpers.h"
#include <UnrealDef.hpp>
#include <Unreal/FProperty.hpp>
#include <Unreal/UClass.hpp>
#include <Unreal/UFunction.hpp>
#include <Unreal/UObject.hpp>
#include <Unreal/UObjectGlobals.hpp>
#include <Unreal/Property/FBoolProperty.hpp>
#include <array>
#include <cstddef>
#include <format>
#include <string>

namespace kh3ap::save_point_detail
{
    using namespace RC;
    using namespace RC::Unreal;
    using save_point_native::format_scalar_property_value;
    using save_point_text::bool_text;
    using save_point_text::sanitize_name_token;
    using save_point_text::string_field_or;

    auto parse_request(const std::string& text, const RuntimeSavePointSpawnDeps& deps)
        -> SavePointSpawnRequest
    {
        SavePointSpawnRequest request{};
        request.request_id =
            JsonFieldReader::string_field(text, "request_id").value_or(std::wstring{});
        request.player_start_tag = JsonFieldReader::string_field(text, "player_start_tag")
                                       .value_or(std::wstring{deps.default_player_start_tag});
        request.runtime_player_start_tag =
            JsonFieldReader::string_field(text, "runtime_player_start_tag")
                .value_or(std::wstring{});
        request.disp_map_name =
            string_field_or(text, "disp_map_name", "map_name", request.player_start_tag);
        request.my_reactor_path =
            string_field_or(text, "my_reactor_path", "my_reactor", deps.default_my_reactor_path);
        request.my_recover_path =
            string_field_or(text, "my_recover_path", "my_recover", deps.default_my_recover_path);
        request.my_map_marker_path = string_field_or(text, "my_map_marker_path", "my_map_marker",
                                                     deps.default_my_map_marker_path);
        request.is_auto_save = JsonFieldReader::bool_field(text, "is_auto_save").value_or(true);
        request.force_activate = JsonFieldReader::bool_field(text, "force_activate").value_or(true);
        request.force_component_engine_activation =
            JsonFieldReader::bool_field(text, "force_component_engine_activation").value_or(true);
        request.disable_blocking_collision =
            JsonFieldReader::bool_field(text, "disable_blocking_collision").value_or(true);
        request.spawn_runtime_player_start =
            JsonFieldReader::bool_field(text, "spawn_runtime_player_start").value_or(false);
        request.use_runtime_player_start_tag_for_save_point =
            JsonFieldReader::bool_field(text, "use_runtime_player_start_tag_for_save_point")
                .value_or(true);

        const auto x = JsonFieldReader::number_field(text, "x");
        const auto y = JsonFieldReader::number_field(text, "y");
        const auto z = JsonFieldReader::number_field(text, "z");
        if (x.has_value() && y.has_value() && z.has_value())
        {
            request.location = SpawnVector{*x, *y, *z};
        }
        return request;
    }

    auto choose_runtime_player_start_tag(const SavePointSpawnRequest& request) -> std::wstring
    {
        if (!request.runtime_player_start_tag.empty())
        {
            return request.runtime_player_start_tag;
        }

        std::wstring suffix = sanitize_name_token(request.request_id, runtime_name_suffix_limit);
        if (suffix.empty())
        {
            suffix = sanitize_name_token(request.player_start_tag, runtime_name_suffix_limit);
        }
        if (suffix.empty())
        {
            suffix = STR("Manual");
        }
        return std::wstring{STR("AP_RT_Save_")} + suffix;
    }

    auto write_bool_metadata_property(UObject* actor, std::wstring_view stage,
                                      std::wstring_view property_name, bool value)
        -> MetadataWriteSummary
    {
        MetadataWriteSummary summary{};
        summary.stage = std::wstring{stage};
        summary.property_name = std::wstring{property_name};
        summary.requested_value = bool_text(value);

        // spawn_save_point owns this synchronous setup chain and has
        // already validated the newly spawned actor. Re-running the global
        // object validity scan for every property costs tens of ms on
        // object-heavy maps.
        if (!actor)
        {
            summary.status = STR("failed");
            summary.reason = STR("invalid_actor");
            return summary;
        }

        const std::wstring owned_property_name{property_name};
        FProperty* const property = actor->GetPropertyByNameInChain(owned_property_name.c_str());
        if (!property)
        {
            summary.status = STR("failed");
            summary.reason = STR("missing_property");
            summary.final_value = STR("<missing>");
            return summary;
        }
        if (property->GetClass().GetName() != STR("BoolProperty"))
        {
            summary.status = STR("failed");
            summary.reason =
                std::format(STR("non_bool_property:{}"), property->GetClass().GetName());
            summary.final_value = format_scalar_property_value(actor, property_name);
            return summary;
        }

        auto* const bool_property = static_cast<FBoolProperty*>(property);
        const bool old_value = bool_property->GetPropertyValueInContainer(actor);
        bool_property->SetPropertyValueInContainer(actor, value);
        summary.wrote = true;
        summary.status =
            old_value == value ? std::wstring{STR("already")} : std::wstring{STR("written")};
        summary.final_value = bool_text(value);
        return summary;
    }

    auto write_name_metadata_property(UObject* actor, std::wstring_view stage,
                                      std::wstring_view property_name, std::wstring_view value)
        -> MetadataWriteSummary
    {
        MetadataWriteSummary summary{};
        summary.stage = std::wstring{stage};
        summary.property_name = std::wstring{property_name};
        summary.requested_value = std::wstring{value};

        if (!actor)
        {
            summary.status = STR("failed");
            summary.reason = STR("invalid_actor");
            return summary;
        }
        if (value.empty())
        {
            summary.status = STR("skipped");
            summary.reason = STR("empty_value");
            summary.final_value = format_scalar_property_value(actor, property_name);
            return summary;
        }

        const std::wstring owned_property_name{property_name};
        FProperty* const property = actor->GetPropertyByNameInChain(owned_property_name.c_str());
        if (!property)
        {
            summary.status = STR("failed");
            summary.reason = STR("missing_property");
            summary.final_value = STR("<missing>");
            return summary;
        }

        const std::wstring property_class = property->GetClass().GetName();
        if (property_class != STR("NameProperty") && property_class != STR("StrProperty"))
        {
            summary.status = STR("failed");
            summary.reason = std::format(STR("non_name_or_string_property:{}"), property_class);
            summary.final_value = format_scalar_property_value(actor, property_name);
            return summary;
        }

        const auto current_value =
            try_read_object_scalar_property_as_string(actor, owned_property_name.c_str());
        if (current_value.has_value() && *current_value == value)
        {
            summary.status = STR("already");
            summary.wrote = true;
            summary.final_value = *current_value;
            return summary;
        }

        if (!try_write_object_name_or_string_property(actor, property_name, value))
        {
            summary.status = STR("failed");
            summary.reason = STR("write_failed");
            summary.final_value = format_scalar_property_value(actor, property_name);
            return summary;
        }

        summary.status = STR("written");
        summary.wrote = true;
        summary.final_value = std::wstring{value};
        return summary;
    }

    auto append_save_point_metadata_writes(SavePointSetupSummary* setup_summary, UObject* actor,
                                           const SavePointSpawnRequest& request,
                                           std::wstring_view stage) -> void
    {
        if (!setup_summary)
        {
            return;
        }

        setup_summary->metadata_writes.push_back(
            write_bool_metadata_property(actor, stage, STR("m_IsAutoSave"), request.is_auto_save));
        setup_summary->metadata_writes.push_back(write_name_metadata_property(
            actor, stage, STR("m_DispMapName"), request.disp_map_name));
        setup_summary->metadata_writes.push_back(
            write_bool_metadata_property(actor, stage, STR("m_bAutoActivate"), true));
        setup_summary->metadata_writes.push_back(write_bool_metadata_property(
            actor, stage, STR("m_bGimmickComponentAutoLinkActivate"), true));
        setup_summary->metadata_writes.push_back(
            write_bool_metadata_property(actor, stage, STR("m_bIsActive"), true));
    }

    auto append_runtime_player_start_metadata_writes(RuntimePlayerStartSpawnSummary* summary,
                                                     UObject* actor,
                                                     const SavePointSpawnRequest& request,
                                                     std::wstring_view tag, std::wstring_view stage)
        -> void
    {
        if (!summary)
        {
            return;
        }

        summary->metadata_writes.push_back(
            write_name_metadata_property(actor, stage, STR("PlayerStartTag"), tag));
        summary->metadata_writes.push_back(write_name_metadata_property(
            actor, stage, STR("m_DispMapName"), request.disp_map_name));
        summary->metadata_writes.push_back(
            write_bool_metadata_property(actor, stage, STR("m_IsAutoSave"), request.is_auto_save));
        summary->metadata_writes.push_back(
            write_bool_metadata_property(actor, stage, STR("m_IsEnableNavMap"), true));
        summary->metadata_writes.push_back(
            write_bool_metadata_property(actor, stage, STR("m_Enabled"), true));
    }

    auto try_write_player_start_tag(UObject* actor, std::wstring_view value) -> std::wstring
    {
        if (value.empty())
        {
            return STR("<empty>");
        }

        constexpr std::array<std::wstring_view, 6> property_names{
            STR("PlayerStartTag"),  STR("PlayerStart"),      STR("PlayerStartPosition"),
            STR("PlayerStartName"), STR("InPlayerStartTag"), STR("Player Start Tag"),
        };
        for (std::wstring_view property_name : property_names)
        {
            if (try_write_object_name_or_string_property(actor, property_name, value))
            {
                return std::wstring{property_name};
            }
        }
        return STR("<no_matching_property>");
    }
} // namespace kh3ap::save_point_detail
