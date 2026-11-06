#include "kh3ap/features/party/party_helpers.h"
#include "kh3ap/features/party/party_map_policy.h"
#include "kh3ap/features/grants/grant_internal_call_depth.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/core/objects/uobject_validity.h"

#include <DynamicOutput/DynamicOutput.hpp>
#include <Unreal/FProperty.hpp>
#include <Unreal/FWeakObjectPtr.hpp>
#include <Unreal/Property/FBoolProperty.hpp>
#include <Unreal/Property/FStructProperty.hpp>
#include <Unreal/UScriptStruct.hpp>
#include <Unreal/UClass.hpp>
#include <Unreal/UFunction.hpp>
#include <Unreal/UObject.hpp>
#include <Unreal/UObjectGlobals.hpp>
#include <UnrealDef.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>

namespace kh3ap::party_native
{
    using namespace RC;
    using namespace RC::Unreal;

    namespace
    {
        constexpr int maximum_persistent_level_outer_depth = 8;
        constexpr std::size_t actor_position_size = sizeof(std::array<float, 3>);
        // TresNpcLocomotionDefinitionLand_FollowPlayer's shipped default.
        constexpr double native_follow_distance = 3000.0;
    } // namespace

    [[nodiscard]] auto read_object_like_property(UObject* object, std::wstring_view property_name)
        -> UObject*
    {
        if (!object || !is_uobject_valid_fast(object))
        {
            return nullptr;
        }
        const std::wstring owned_name{property_name};
        FProperty* const property = object->GetPropertyByNameInChain(owned_name.c_str());
        void* const value_ptr = object->GetValuePtrByPropertyNameInChain(owned_name.c_str());
        if (!property || !value_ptr ||
            (property->GetClass().GetName() != STR("ObjectProperty") &&
             property->GetClass().GetName() != STR("ClassProperty")) ||
            property->GetElementSize() < static_cast<int>(sizeof(UObject*)))
        {
            return nullptr;
        }
        UObject* value = nullptr;
        std::memcpy(&value, value_ptr, sizeof(value));
        return value && is_uobject_valid_fast(value) ? value : nullptr;
    }

    auto write_object_like_property(UObject* object, std::wstring_view property_name,
                                    UObject* value) -> bool
    {
        if (!object || !is_uobject_valid_fast(object) || !value || !is_uobject_valid_fast(value))
        {
            return false;
        }
        const std::wstring owned_name{property_name};
        FProperty* const property = object->GetPropertyByNameInChain(owned_name.c_str());
        void* const value_ptr = object->GetValuePtrByPropertyNameInChain(owned_name.c_str());
        if (!property || !value_ptr ||
            (property->GetClass().GetName() != STR("ObjectProperty") &&
             property->GetClass().GetName() != STR("ClassProperty")) ||
            property->GetElementSize() < static_cast<int>(sizeof(UObject*)))
        {
            return false;
        }
        std::memcpy(value_ptr, &value, sizeof(value));
        return true;
    }

    auto run_behavior_tree(UObject* controller, UObject* behavior_tree) -> bool
    {
        if (!controller || !is_uobject_valid_fast(controller) || !behavior_tree ||
            !is_uobject_valid_fast(behavior_tree))
        {
            return false;
        }
        UFunction* const function = controller->GetFunctionByNameInChain(STR("RunBehaviorTree"));
        if (!function || !is_uobject_valid_fast(function) || function->GetParmsSize() <= 0)
        {
            return false;
        }
        std::vector<std::byte> params(static_cast<std::size_t>(function->GetParmsSize()),
                                      std::byte{0});
        if (!write_object_function_param(function, params.data(), STR("BTAsset"), behavior_tree))
        {
            return false;
        }
        const RuntimeGrantInternalCallDepth::Scope internal_call_scope{};
        controller->ProcessEvent(function, params.data());
        return true;
    }

    [[nodiscard]] auto persistent_level_for(UObject* object) -> UObject*
    {
        for (int depth = 0; object && is_uobject_valid_fast(object) &&
                            depth < maximum_persistent_level_outer_depth;
             ++depth)
        {
            if (object->GetName() == STR("PersistentLevel"))
            {
                return object;
            }
            object = object->GetOuterPrivate();
        }
        return nullptr;
    }

    auto configure_party_navigation(UObject* pawn, UObject* controller) -> bool
    {
        UObject* const level = persistent_level_for(pawn);
        UObject* const path_following =
            read_object_like_property(controller, STR("PathFollowingComponent"));
        UObject* const current_nav_data =
            read_object_like_property(path_following, STR("MyNavData"));
        if (!level || !current_nav_data)
        {
            return true;
        }
        const auto fallback =
            runtime_party_navigation_fallback(current_nav_data->GetName(), level->GetFullName());
        if (fallback.mesh_name.empty())
        {
            return true;
        }
        // The audited current mesh is empty or lacks follow coverage here.
        // Keep collision unchanged; use the map's fallback profile for both
        // EQS agent selection and path following.
        // The policy above accepts only exact persistent-level names.
        // Retry can load another level with the same full path. Supplying
        // its live outer also avoids UE4SS's global full-name cache, which
        // may still return the previous map's navigation object.
        UObject* const nav_data = UObjectGlobals::StaticFindObject<UObject*>(
            current_nav_data->GetClassPrivate(), level, fallback.mesh_name);
        UObject* const movement = read_object_like_property(pawn, STR("MyMovement"));
        if (!nav_data || !is_uobject_valid_fast(nav_data) ||
            persistent_level_for(nav_data) != level || !movement || !path_following)
        {
            return false;
        }
        FProperty* const agent_property = movement->GetPropertyByNameInChain(STR("NavAgentProps"));
        FProperty* const update_property =
            movement->GetPropertyByNameInChain(STR("bUpdateNavAgentWithOwnersCollision"));
        if (!agent_property || agent_property->GetClass().GetName() != STR("StructProperty") ||
            !update_property || update_property->GetClass().GetName() != STR("BoolProperty"))
        {
            return false;
        }
        auto* const agent_struct = static_cast<FStructProperty*>(agent_property)->GetStruct();
        if (!agent_struct || !is_uobject_valid_fast(agent_struct))
        {
            return false;
        }
        FProperty* const radius_property =
            agent_struct->GetPropertyByNameInChain(STR("AgentRadius"));
        FProperty* const height_property =
            agent_struct->GetPropertyByNameInChain(STR("AgentHeight"));
        if (!radius_property || !height_property ||
            radius_property->GetClass().GetName() != STR("FloatProperty") ||
            height_property->GetClass().GetName() != STR("FloatProperty") ||
            radius_property->GetElementSize() != sizeof(float) ||
            height_property->GetElementSize() != sizeof(float))
        {
            return false;
        }
        void* const agent = agent_property->ContainerPtrToValuePtr<void>(movement);
        float* const radius = radius_property->ContainerPtrToValuePtr<float>(agent);
        float* const height = height_property->ContainerPtrToValuePtr<float>(agent);
        if (!agent || !radius || !height ||
            !write_object_like_property(path_following, STR("MyNavData"), nav_data))
        {
            return false;
        }
        static_cast<FBoolProperty*>(update_property)->SetPropertyValueInContainer(movement, false);
        *radius = fallback.radius;
        *height = fallback.height;
        Output::send<LogLevel::Normal>(
            STR("[KH3APProbeCpp] Party navigation uses {} pawn={} radius={} height={}\n"),
            fallback.mesh_name, pawn->GetFullName(), *radius, *height);
        return true;
    }

    [[nodiscard]] auto resolve_named_function(UObject* object, std::wstring_view function_name)
        -> UFunction*
    {
        if (!object || !is_uobject_valid_fast(object) || function_name.empty())
        {
            return nullptr;
        }
        const std::wstring owned_name{function_name};
        UFunction* const function = object->GetFunctionByNameInChain(owned_name.c_str());
        return function && is_uobject_valid_fast(function) ? function : nullptr;
    }

    [[nodiscard]] auto invoke_named_bool_getter(UObject* object, std::wstring_view function_name)
        -> std::optional<bool>
    {
        UFunction* const function = resolve_named_function(object, function_name);
        FProperty* return_property = nullptr;
        if (!function || function->GetParmsSize() <= 0)
        {
            return std::nullopt;
        }
        for (FProperty* param = function->GetFirstProperty(); param;
             param = param->HasNext() ? param->GetNextFieldAsProperty() : nullptr)
        {
            if (!param->HasAnyPropertyFlags(CPF_Parm))
            {
                continue;
            }
            if (!param->HasAnyPropertyFlags(CPF_ReturnParm) ||
                param->GetClass().GetName() != STR("BoolProperty") || return_property)
            {
                return std::nullopt;
            }
            return_property = param;
        }
        if (!return_property)
        {
            return std::nullopt;
        }

        std::vector<std::byte> params(static_cast<std::size_t>(function->GetParmsSize()),
                                      std::byte{0});
        const RuntimeGrantInternalCallDepth::Scope internal_call_scope{};
        object->ProcessEvent(function, params.data());
        bool* const value = return_property->ContainerPtrToValuePtr<bool>(params.data());
        return value ? std::optional<bool>{*value} : std::nullopt;
    }

    auto invoke_named_single_bool_function(UObject* object, std::wstring_view function_name,
                                           bool value) -> bool
    {
        UFunction* const function = resolve_named_function(object, function_name);
        FProperty* input_property = nullptr;
        if (!function || function->GetParmsSize() <= 0)
        {
            return false;
        }
        for (FProperty* param = function->GetFirstProperty(); param;
             param = param->HasNext() ? param->GetNextFieldAsProperty() : nullptr)
        {
            if (!param->HasAnyPropertyFlags(CPF_Parm) || param->HasAnyPropertyFlags(CPF_ReturnParm))
            {
                continue;
            }
            if (param->GetClass().GetName() != STR("BoolProperty") || input_property)
            {
                return false;
            }
            input_property = param;
        }
        if (!input_property)
        {
            return false;
        }

        std::vector<std::byte> params(static_cast<std::size_t>(function->GetParmsSize()),
                                      std::byte{0});
        if (!write_bool_function_param(input_property, params.data(), value))
        {
            return false;
        }
        const RuntimeGrantInternalCallDepth::Scope internal_call_scope{};
        object->ProcessEvent(function, params.data());
        return true;
    }

    auto actor_position(UObject* object) -> std::optional<std::array<float, 3>>
    {
        UFunction* const function = resolve_named_function(object, STR("K2_GetActorLocation"));
        if (!function || function->GetParmsSize() != actor_position_size)
        {
            return std::nullopt;
        }
        FProperty* const property = find_function_param_by_name(function, STR("ReturnValue"));
        if (!property || property->GetClass().GetName() != STR("StructProperty") ||
            property->GetElementSize() != actor_position_size ||
            static_cast<FStructProperty*>(property)->GetStruct()->GetName() != STR("Vector"))
        {
            return std::nullopt;
        }
        std::array<std::byte, actor_position_size> params{};
        const RuntimeGrantInternalCallDepth::Scope internal_call_scope{};
        object->ProcessEvent(function, params.data());
        std::array<float, 3> result{};
        std::memcpy(result.data(), property->ContainerPtrToValuePtr<void>(params.data()),
                    actor_position_size);
        if (!std::all_of(result.begin(), result.end(),
                         [](float value)
                         {
                             return std::isfinite(value);
                         }))
        {
            return std::nullopt;
        }
        return result;
    }

    auto outside_native_follow_distance(const std::array<float, 3>& first,
                                        const std::array<float, 3>& second) -> bool
    {
        // TresNpcLocomotionDefinitionLand_FollowPlayer's shipped default.
        double squared_distance = 0;
        for (std::size_t axis = 0; axis < first.size(); ++axis)
        {
            const double delta = static_cast<double>(first[axis]) - second[axis];
            squared_distance += delta * delta;
        }
        return squared_distance > native_follow_distance * native_follow_distance;
    }

    [[nodiscard]] auto invoke_bool_getter(UObject* object, std::wstring_view function_path)
        -> std::optional<bool>
    {
        if (!object || !is_uobject_valid_fast(object))
        {
            return std::nullopt;
        }
        const std::wstring name{function_path.substr(function_path.rfind(L':') + 1)};
        UFunction* const function = object->GetFunctionByNameInChain(name.c_str());
        FProperty* const return_property =
            function ? find_function_param_by_name(function, STR("ReturnValue")) : nullptr;
        if (!function || !is_uobject_valid_fast(function) || !return_property ||
            return_property->GetClass().GetName() != STR("BoolProperty") ||
            function->GetParmsSize() <= 0)
        {
            return std::nullopt;
        }

        std::vector<std::byte> params(static_cast<std::size_t>(function->GetParmsSize()),
                                      std::byte{0});
        const RuntimeGrantInternalCallDepth::Scope internal_call_scope{};
        object->ProcessEvent(function, params.data());
        bool* const value = return_property->ContainerPtrToValuePtr<bool>(params.data());
        return value ? std::optional<bool>{*value} : std::nullopt;
    }

    auto invoke_set_disable_battle_mode_ai(UObject* pawn, bool disabled) -> bool
    {
        if (!pawn || !is_uobject_valid_fast(pawn))
        {
            return false;
        }
        UFunction* const function = pawn->GetFunctionByNameInChain(STR("SetDisableBattleModeAI"));
        if (!function || !is_uobject_valid_fast(function) || function->GetParmsSize() <= 0)
        {
            return false;
        }
        std::vector<std::byte> params(static_cast<std::size_t>(function->GetParmsSize()),
                                      std::byte{0});
        if (!write_bool_function_param(find_function_param_by_name(function, STR("InDisable")),
                                       params.data(), disabled))
        {
            return false;
        }
        const RuntimeGrantInternalCallDepth::Scope internal_call_scope{};
        pawn->ProcessEvent(function, params.data());
        return true;
    }
    [[nodiscard]] auto is_friend_component(UObject* object) -> bool
    {
        if (!object || !is_uobject_valid_fast(object))
        {
            return false;
        }
        UClass* const object_class = object->GetClassPrivate();
        return object_class && is_uobject_valid_fast(object_class) &&
               object_class->GetName() == STR("TresFriendComponent");
    }
    auto read_friend_slots(UObject* manager, std::span<UObject*> slots) -> bool
    {
        std::fill(slots.begin(), slots.end(), nullptr);
        if (!is_friend_component(manager))
        {
            return false;
        }
        FProperty* const property = manager->GetPropertyByNameInChain(STR("m_pFriendPawn"));
        void* const values = manager->GetValuePtrByPropertyNameInChain(STR("m_pFriendPawn"));
        if (!property || !values || property->GetClass().GetName() != STR("WeakObjectProperty") ||
            property->GetArrayDim() < static_cast<int32_t>(slots.size()) ||
            property->GetElementSize() != sizeof(FWeakObjectPtr))
        {
            return false;
        }
        for (size_t slot = 0; slot < slots.size(); ++slot)
        {
            FWeakObjectPtr weak{};
            std::memcpy(&weak, static_cast<std::byte*>(values) + slot * sizeof(weak), sizeof(weak));
            UObject* const pawn = weak.Get();
            if (pawn && is_uobject_valid_fast(pawn))
            {
                slots[slot] = pawn;
            }
        }
        return true;
    }
} // namespace kh3ap::party_native
