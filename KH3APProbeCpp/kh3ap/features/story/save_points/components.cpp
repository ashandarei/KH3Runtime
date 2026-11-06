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
#include <algorithm>
#include <array>
#include <cstddef>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace kh3ap::save_point_detail
{
    using namespace RC;
    using namespace RC::Unreal;
    using save_point_activation::call_actor_bool_function;
    using save_point_activation::call_actor_function_no_params;
    using save_point_activation::call_actor_integral_function;
    using save_point_activation::call_actor_two_bool_function;
    using save_point_activation::call_actor_two_integral_function;
    using save_point_native::describe_object_property;
    using save_point_native::try_read_object_property;
    using save_point_native::try_read_object_property_trusted;
    using save_point_native::try_resolve_cached_function;
    using save_point_native::try_resolve_object_path;
    using save_point_native::try_write_object_property;

    auto ensure_component_property(UObject* actor, std::wstring_view property_name,
                                   std::wstring_view requested_path) -> ComponentAssignmentSummary
    {
        ComponentAssignmentSummary summary{};
        summary.property_name = std::wstring{property_name};
        summary.requested_path = std::wstring{requested_path};

        UObject* const current_value = try_read_object_property(actor, property_name);
        if (current_value)
        {
            summary.status = STR("existing");
            summary.final_value = STR("<existing>");
            return summary;
        }

        if (requested_path.empty())
        {
            summary.status = STR("skipped");
            summary.reason = STR("empty_path");
            summary.final_value = describe_object_property(actor, property_name);
            return summary;
        }

        std::wstring lookup_path{};
        UObject* const resolved_object = try_resolve_object_path(requested_path, &lookup_path);
        summary.lookup_path = lookup_path;
        summary.resolved_object = format_object_debug_name(resolved_object);
        if (!resolved_object || !kh3ap::is_uobject_valid_fast(resolved_object))
        {
            summary.status = STR("failed");
            summary.reason = STR("path_not_loaded_or_not_found");
            summary.final_value = describe_object_property(actor, property_name);
            return summary;
        }

        std::wstring write_reason{};
        if (!try_write_object_property(actor, property_name, resolved_object, &write_reason))
        {
            summary.status = STR("failed");
            summary.reason =
                write_reason.empty() ? std::wstring{STR("write_failed")} : write_reason;
            summary.final_value = describe_object_property(actor, property_name);
            return summary;
        }

        summary.status = STR("assigned");
        summary.wrote = true;
        summary.final_value = describe_object_property(actor, property_name);
        return summary;
    }

    auto ensure_save_point_component_properties(UObject* actor,
                                                const SavePointSpawnRequest& request)
        -> std::array<ComponentAssignmentSummary, 3>
    {
        return {
            ensure_component_property(actor, STR("MyReactor"), request.my_reactor_path),
            ensure_component_property(actor, STR("MyRecover"), request.my_recover_path),
            ensure_component_property(actor, STR("MyMapMarker"), request.my_map_marker_path),
        };
    }

    auto try_call_bool_return_function(UObject* object, std::wstring_view function_path)
        -> std::optional<bool>
    {
        if (!object || !kh3ap::is_uobject_valid_fast(object) || function_path.empty())
        {
            return std::nullopt;
        }

        UFunction* const function_object = try_resolve_cached_function(function_path);
        if (!function_object)
        {
            return std::nullopt;
        }

        FProperty* const return_property =
            find_function_param_by_name(function_object, STR("ReturnValue"));
        if (!return_property || return_property->GetClass().GetName() != STR("BoolProperty"))
        {
            return std::nullopt;
        }

        std::vector<std::byte> params(static_cast<size_t>(function_object->GetParmsSize()));
        std::fill(params.begin(), params.end(), std::byte{0});
        {
            const RuntimeGrantInternalCallDepth::Scope internal_call_scope{};
            object->ProcessEvent(function_object, params.data());
        }

        if (bool* const return_value = return_property->ContainerPtrToValuePtr<bool>(params.data()))
        {
            return *return_value;
        }
        return std::nullopt;
    }

    auto try_call_object_return_function(UObject* object, std::wstring_view function_path)
        -> UObject*
    {
        if (!object || !kh3ap::is_uobject_valid_fast(object) || function_path.empty())
        {
            return nullptr;
        }

        UFunction* const function_object = try_resolve_cached_function(function_path);
        if (!function_object)
        {
            return nullptr;
        }

        FProperty* const return_property =
            find_function_param_by_name(function_object, STR("ReturnValue"));
        if (!return_property || return_property->GetClass().GetName() != STR("ObjectProperty"))
        {
            return nullptr;
        }

        std::vector<std::byte> params(static_cast<size_t>(function_object->GetParmsSize()));
        std::fill(params.begin(), params.end(), std::byte{0});
        {
            const RuntimeGrantInternalCallDepth::Scope internal_call_scope{};
            object->ProcessEvent(function_object, params.data());
        }

        if (UObject** const return_value =
                return_property->ContainerPtrToValuePtr<UObject*>(params.data()))
        {
            return *return_value;
        }
        return nullptr;
    }

    auto collect_save_point_component_objects(UObject* actor)
        -> std::vector<std::pair<std::wstring_view, UObject*>>
    {
        std::vector<std::pair<std::wstring_view, UObject*>> objects{};
        // This helper is only used while synchronously configuring the
        // freshly spawned actor. The spawn path already validated the
        // actor and owns these component references for this whole call,
        // so repeating a global object-array scan for every property is
        // both redundant and especially expensive on object-heavy maps.
        objects.emplace_back(STR("RootComponent"),
                             try_read_object_property_trusted(actor, STR("RootComponent")));
        objects.emplace_back(STR("CapsuleComponent"),
                             try_read_object_property_trusted(actor, STR("CapsuleComponent")));
        objects.emplace_back(STR("MyReactor"),
                             try_read_object_property_trusted(actor, STR("MyReactor")));
        objects.emplace_back(STR("MyRecover"),
                             try_read_object_property_trusted(actor, STR("MyRecover")));
        objects.emplace_back(STR("MyMapMarker"),
                             try_read_object_property_trusted(actor, STR("MyMapMarker")));
        return objects;
    }

    auto collect_runtime_player_start_component_objects(UObject* actor)
        -> std::vector<std::pair<std::wstring_view, UObject*>>
    {
        std::vector<std::pair<std::wstring_view, UObject*>> objects{};
        objects.emplace_back(STR("RootComponent"),
                             try_read_object_property(actor, STR("RootComponent")));
        objects.emplace_back(STR("CapsuleComponent"),
                             try_read_object_property(actor, STR("CapsuleComponent")));
        return objects;
    }

    auto prepare_runtime_player_start_actor(RuntimePlayerStartSpawnSummary* summary, UObject* actor)
        -> void
    {
        if (!summary || !actor || !kh3ap::is_uobject_valid_fast(actor))
        {
            return;
        }

        summary->activation_calls.push_back(call_actor_bool_function(
            actor, STR("/Script/Engine.Actor:SetActorHiddenInGame"), STR("bNewHidden"), true));
        summary->activation_calls.push_back(
            call_actor_bool_function(actor, STR("/Script/Engine.Actor:SetActorEnableCollision"),
                                     STR("bNewActorEnableCollision"), false));

        for (const auto& [label, component] : collect_runtime_player_start_component_objects(actor))
        {
            (void)label;
            if (!component || !kh3ap::is_uobject_valid_fast(component))
            {
                continue;
            }

            summary->activation_calls.push_back(call_actor_integral_function(
                component, STR("/Script/Engine.PrimitiveComponent:SetCollisionEnabled"),
                STR("NewType"), 0));
            summary->activation_calls.push_back(call_actor_bool_function(
                component, STR("/Script/Engine.PrimitiveComponent:SetGenerateOverlapEvents"),
                STR("bInGenerateOverlapEvents"), false));
        }
    }

    auto force_engine_component_state(SavePointSetupSummary* setup_summary, UObject* actor,
                                      const SavePointSpawnRequest& request) -> void
    {
        if (!setup_summary || !request.force_component_engine_activation || !actor)
        {
            return;
        }

        setup_summary->activation_calls.push_back(call_actor_bool_function(
            actor, STR("/Script/Engine.Actor:SetActorHiddenInGame"), STR("bNewHidden"), false));
        setup_summary->activation_calls.push_back(
            call_actor_bool_function(actor, STR("/Script/Engine.Actor:SetActorEnableCollision"),
                                     STR("bNewActorEnableCollision"), true));

        std::vector<UObject*> observed_components{};
        std::vector<UObject*> generic_state_components{};
        for (const auto& [label, component] : collect_save_point_component_objects(actor))
        {
            if (!component)
            {
                continue;
            }

            const bool already_observed =
                std::find(observed_components.begin(), observed_components.end(), component) !=
                observed_components.end();
            if (!already_observed)
            {
                // These are direct references owned by the freshly
                // spawned actor and are consumed before this synchronous
                // setup chain yields. Avoid a global IsReal scan for each
                // unique component; duplicate labels are still retained
                // below because CapsuleComponent has distinct collision
                // policy even when it is also RootComponent.
                observed_components.push_back(component);
            }

            // Activate already calls SetActive(true, bReset) internally;
            // forcing AutoActivate and tick state after FinishSpawningActor
            // duplicated the component's normal BeginPlay setup.
            const bool generic_state_already_applied =
                std::find(generic_state_components.begin(), generic_state_components.end(),
                          component) != generic_state_components.end();
            if (!generic_state_already_applied)
            {
                generic_state_components.push_back(component);
                setup_summary->activation_calls.push_back(call_actor_bool_function(
                    component, STR("/Script/Engine.ActorComponent:Activate"), STR("bReset"), true));

                if (label != STR("MyMapMarker"))
                {
                    setup_summary->activation_calls.push_back(call_actor_two_bool_function(
                        component, STR("/Script/Engine.SceneComponent:SetVisibility"),
                        STR("bNewVisibility"), true, STR("bPropagateToChildren"), true));
                    setup_summary->activation_calls.push_back(call_actor_two_bool_function(
                        component, STR("/Script/Engine.SceneComponent:SetHiddenInGame"),
                        STR("NewHidden"), false, STR("bPropagateToChildren"), true));
                }
            }

            // RootComponent and CapsuleComponent can name the same UObject.
            // Generic state is deduplicated above, while label-specific
            // collision policy still runs for the CapsuleComponent entry.
            if (label != STR("MyMapMarker"))
            {
                if (request.disable_blocking_collision && label == STR("CapsuleComponent"))
                {
                    setup_summary->activation_calls.push_back(call_actor_integral_function(
                        component, STR("/Script/Engine.PrimitiveComponent:SetCollisionEnabled"),
                        STR("NewType"), 0));
                }
                else if (label == STR("MyReactor") || label == STR("MyRecover"))
                {
                    setup_summary->activation_calls.push_back(call_actor_integral_function(
                        component, STR("/Script/Engine.PrimitiveComponent:SetCollisionEnabled"),
                        STR("NewType"), 1));
                    if (request.disable_blocking_collision)
                    {
                        setup_summary->activation_calls.push_back(call_actor_two_integral_function(
                            component,
                            STR("/Script/Engine.PrimitiveComponent:SetCollisionResponseToChannel"),
                            STR("Channel"), 2, STR("NewResponse"), 1));
                    }
                }
            }

            // BP_ActivateGimmickComponentAll below owns the Blueprint-level
            // component activation pass for the completed save-point actor.
        }
    }

    auto activate_save_point_actor(SavePointSetupSummary* setup_summary, UObject* actor,
                                   const SavePointSpawnRequest& request) -> void
    {
        if (!setup_summary || !request.force_activate)
        {
            return;
        }

        force_engine_component_state(setup_summary, actor, request);
        setup_summary->activation_calls.push_back(call_actor_bool_function(
            actor, STR("/Script/TresGame.TresSavePointActor:BP_SetDisableMode"), STR("bIsDisable"),
            false));
        setup_summary->activation_calls.push_back(call_actor_bool_function(
            actor, STR("/Script/TresGame.TresSavePointActor:OnChangeBattleMode"),
            STR("bIsBattleMode"), false));
        setup_summary->activation_calls.push_back(call_actor_bool_function(
            actor, STR("/Script/TresGame.TresSavePointActor:OnChangeCinematicMode"),
            STR("bIsCinematicMode"), false));
        setup_summary->activation_calls.push_back(call_actor_bool_function(
            actor, STR("/Script/TresGame.TresSavePointActor:OnChangePlayerArtsMode"),
            STR("bIsArtsMode"), false));
        setup_summary->activation_calls.push_back(call_actor_integral_function(
            actor, STR("/Script/TresGame.TresSavePointActor:OnChangeSavePointDispType"),
            STR("InDispType"), 0));
        setup_summary->activation_calls.push_back(call_actor_bool_function(
            actor, STR("/Script/TresGame.TresSavePointActor:ReceiveGimmickActivate"),
            STR("bEnable"), true));
        setup_summary->activation_calls.push_back(call_actor_function_no_params(
            actor, STR("/Script/TresGame.TresSavePointActor:BP_ActivateGimmickComponentAll")));
        setup_summary->activation_calls.push_back(call_actor_function_no_params(
            actor, STR("/Script/TresGame.TresSavePointActor:BP_ActivateGimmick")));
    }
} // namespace kh3ap::save_point_detail
