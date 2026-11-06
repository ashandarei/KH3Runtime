#include "kh3ap/features/story/save_points/internal.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/bridge/utilities.h"
#include <UnrealDef.hpp>
#include <Unreal/FProperty.hpp>
#include <Unreal/UClass.hpp>
#include <Unreal/UFunction.hpp>
#include <Unreal/UObject.hpp>
#include <Unreal/UObjectGlobals.hpp>
#include <cstddef>
#include <optional>
#include <sstream>
#include <string>

namespace kh3ap::save_point_detail
{
    using namespace RC;
    using namespace RC::Unreal;
    using save_point_native::describe_object_property;
    using save_point_native::format_scalar_property_value;
    using save_point_text::append_json_optional_bool_field;
    using save_point_text::append_json_string_field;

    auto append_property_snapshot_field(std::ostringstream& json, UObject* actor,
                                        std::wstring_view property_name, bool* first) -> void
    {
        if (!first)
        {
            return;
        }
        if (!*first)
        {
            json << ",";
        }
        *first = false;

        const std::wstring owned_property_name{property_name};
        const std::wstring value = format_scalar_property_value(actor, owned_property_name);
        json << "\"" << json_escape(property_name) << "\":\"" << json_escape(value) << "\"";
    }

    auto append_component_state_json(std::ostringstream& json, std::wstring_view label,
                                     UObject* component, bool include_scene_state,
                                     bool include_primitive_state, bool include_tres_gimmick_state)
        -> void
    {
        json << "\"" << json_escape(label) << "\":{";
        json << "\"object\":\"" << json_escape(format_object_debug_name(component)) << "\"";
        const bool is_real = component && kh3ap::is_uobject_valid_fast(component);
        json << ",\"is_real\":" << (is_real ? "true" : "false");
        if (is_real)
        {
            append_json_string_field(
                json, "owner",
                format_object_debug_name(try_call_object_return_function(
                    component, STR("/Script/Engine.ActorComponent:GetOwner"))));
            append_json_string_field(json, "attach_parent",
                                     describe_object_property(component, STR("AttachParent")));
            append_json_optional_bool_field(
                json, "is_active",
                try_call_bool_return_function(component,
                                              STR("/Script/Engine.ActorComponent:IsActive")));
            append_json_optional_bool_field(
                json, "is_being_destroyed",
                try_call_bool_return_function(
                    component, STR("/Script/Engine.ActorComponent:IsBeingDestroyed")));
            append_json_optional_bool_field(
                json, "is_tick_enabled",
                try_call_bool_return_function(
                    component, STR("/Script/Engine.ActorComponent:IsComponentTickEnabled")));
            if (include_primitive_state)
            {
                append_json_optional_bool_field(
                    json, "is_collision_enabled",
                    try_call_bool_return_function(
                        component, STR("/Script/Engine.PrimitiveComponent:K2_IsCollisionEnabled")));
                append_json_optional_bool_field(
                    json, "is_query_collision_enabled",
                    try_call_bool_return_function(
                        component,
                        STR("/Script/Engine.PrimitiveComponent:K2_IsQueryCollisionEnabled")));
                append_json_optional_bool_field(
                    json, "is_physics_collision_enabled",
                    try_call_bool_return_function(
                        component,
                        STR("/Script/Engine.PrimitiveComponent:K2_IsPhysicsCollisionEnabled")));
            }
            if (include_tres_gimmick_state)
            {
                append_json_optional_bool_field(
                    json, "bp_is_gimmick_component_active",
                    try_call_bool_return_function(
                        component,
                        STR("/Script/"
                            "TresGame.TresGimmickComponentBase:BP_IsGimmickComponentActive")));
            }

            json << ",\"properties\":{";
            bool first_property = true;
            append_property_snapshot_field(json, component, STR("bAutoActivate"), &first_property);
            if (include_scene_state)
            {
                append_property_snapshot_field(json, component, STR("bVisible"), &first_property);
                append_property_snapshot_field(json, component, STR("bHiddenInGame"),
                                               &first_property);
            }
            if (include_primitive_state)
            {
                append_property_snapshot_field(json, component, STR("bGenerateOverlapEvents"),
                                               &first_property);
            }
            append_property_snapshot_field(json, component, STR("m_bDisableCommandDisp"),
                                           &first_property);
            append_property_snapshot_field(json, component, STR("m_bDisableTargetMarker"),
                                           &first_property);
            append_property_snapshot_field(json, component, STR("m_bNeedRayCheck"),
                                           &first_property);
            append_property_snapshot_field(json, component, STR("m_CmdTargetActor"),
                                           &first_property);
            append_property_snapshot_field(json, component, STR("m_pBodySetup"), &first_property);
            append_property_snapshot_field(json, component, STR("m_Command"), &first_property);
            append_property_snapshot_field(json, component, STR("m_EnableUpdatePosition"),
                                           &first_property);
            append_property_snapshot_field(json, component, STR("m_MapMarkerType"),
                                           &first_property);
            append_property_snapshot_field(json, component, STR("OnReactorDoCommand"),
                                           &first_property);
            append_property_snapshot_field(json, component, STR("m_bIsGimmickActive"),
                                           &first_property);
            append_property_snapshot_field(json, component, STR("m_bOwnerHaveTeamNo"),
                                           &first_property);
            append_property_snapshot_field(json, component, STR("m_TeamNo"), &first_property);
            json << "}";
        }
        json << "}";
    }

    auto append_actor_gimmick_state_json(std::ostringstream& json, UObject* actor) -> void
    {
        json << ",\"gimmick_state\":{";

        const auto append_optional_bool_member =
            [&json](std::string_view key, const std::optional<bool>& value, bool* first)
        {
            if (!first)
            {
                return;
            }
            if (!*first)
            {
                json << ",";
            }
            *first = false;
            json << "\"" << key << "\":";
            if (value.has_value())
            {
                json << (*value ? "true" : "false");
            }
            else
            {
                json << "null";
            }
        };

        bool first_state = true;
        append_optional_bool_member(
            "bp_is_gimmick_active",
            try_call_bool_return_function(
                actor, STR("/Script/TresGame.TresSavePointActor:BP_IsGimmickActive")),
            &first_state);
        append_optional_bool_member(
            "bp_is_showed_gimmick_for_plan",
            try_call_bool_return_function(
                actor, STR("/Script/TresGame.TresGimmickActor:BP_IsShowedGimmickForPlan")),
            &first_state);
        append_optional_bool_member(
            "bp_is_start_gimmick_for_plan",
            try_call_bool_return_function(
                actor, STR("/Script/TresGame.TresGimmickActor:BP_IsStartGimmickForPlan")),
            &first_state);
        append_optional_bool_member(
            "bp_is_enabled_collision_gimmick_for_plan",
            try_call_bool_return_function(
                actor,
                STR("/Script/TresGame.TresGimmickActor:BP_IsEnabledCollisionGimmickForPlan")),
            &first_state);
        append_optional_bool_member(
            "bp_is_destroyed_gimmick_for_plan",
            try_call_bool_return_function(
                actor, STR("/Script/TresGame.TresGimmickActor:BP_IsDestroyedGimmickForPlan")),
            &first_state);
        append_optional_bool_member(
            "is_power_saving_mode",
            try_call_bool_return_function(
                actor, STR("/Script/TresGame.TresGimmickActor:IsPowerSavingMode")),
            &first_state);

        json << ",\"properties\":{";
        bool first_property = true;
        append_property_snapshot_field(json, actor, STR("m_bUseGimmickInstanceManage"),
                                       &first_property);
        append_property_snapshot_field(json, actor, STR("m_bUseGimmickPauseDisableTick"),
                                       &first_property);
        append_property_snapshot_field(json, actor, STR("m_bUseGimmickPause"), &first_property);
        append_property_snapshot_field(json, actor, STR("m_bBeginPlayDisableTickRegister"),
                                       &first_property);
        append_property_snapshot_field(json, actor, STR("m_bAutoStartActiveGimmick"),
                                       &first_property);
        append_property_snapshot_field(json, actor, STR("m_bDefaultEnableCollisionGimmick"),
                                       &first_property);
        append_property_snapshot_field(json, actor, STR("m_bAutoStartEnableCollisionGimmick"),
                                       &first_property);
        append_property_snapshot_field(json, actor, STR("m_bDefaultShowGimmick"), &first_property);
        append_property_snapshot_field(json, actor, STR("m_bAutoStartShowGimmick"),
                                       &first_property);
        append_property_snapshot_field(json, actor, STR("m_bAutoStartGimmick"), &first_property);
        append_property_snapshot_field(json, actor, STR("m_bEnableGimmickControl"),
                                       &first_property);
        append_property_snapshot_field(json, actor, STR("m_bIsActive"), &first_property);
        json << "}";

        json << "}";
    }

    auto append_save_point_deep_state_json(std::ostringstream& json, UObject* actor) -> void
    {
        json << ",\"object_refs\":{";
        json << "\"RootComponent\":\""
             << json_escape(describe_object_property(actor, STR("RootComponent"))) << "\"";
        json << ",\"CapsuleComponent\":\""
             << json_escape(describe_object_property(actor, STR("CapsuleComponent"))) << "\"";
        json << ",\"MyReactor\":\""
             << json_escape(describe_object_property(actor, STR("MyReactor"))) << "\"";
        json << ",\"MyRecover\":\""
             << json_escape(describe_object_property(actor, STR("MyRecover"))) << "\"";
        json << ",\"MyMapMarker\":\""
             << json_escape(describe_object_property(actor, STR("MyMapMarker"))) << "\"";
        json << "}";

        append_actor_gimmick_state_json(json, actor);

        json << ",\"component_state\":{";
        const auto objects = collect_save_point_component_objects(actor);
        for (size_t index = 0; index < objects.size(); ++index)
        {
            if (index > 0)
            {
                json << ",";
            }
            const auto& [label, component] = objects[index];
            const bool include_scene_state = label != STR("MyMapMarker");
            const bool include_primitive_state = label == STR("CapsuleComponent") ||
                                                 label == STR("MyReactor") ||
                                                 label == STR("MyRecover");
            const bool include_tres_gimmick_state =
                label == STR("MyReactor") || label == STR("MyRecover");
            append_component_state_json(json, label, component, include_scene_state,
                                        include_primitive_state, include_tres_gimmick_state);
        }
        json << "}";
    }
} // namespace kh3ap::save_point_detail
