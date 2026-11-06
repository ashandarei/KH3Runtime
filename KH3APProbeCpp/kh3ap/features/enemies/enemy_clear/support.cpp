#include <array>
#include <chrono>
#include "kh3ap/features/enemies/enemy_clear/support.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/features/grants/grant_internal_call_depth.h"
#include "kh3ap/core/events/keyblade_inventory_events.h"
#include "kh3ap/bridge/utilities.h"

#include <Unreal/UObject.hpp>
#include <Unreal/UClass.hpp>
#include <Unreal/UFunction.hpp>
#include <Unreal/FProperty.hpp>
#include <Unreal/AActor.hpp>
#include <Unreal/GameplayStatics.hpp>
#include <Unreal/NameTypes.hpp>
#include <Unreal/Property/FBoolProperty.hpp>
#include <Unreal/Property/FStructProperty.hpp>
#include <Unreal/UObjectGlobals.hpp>
#include <Unreal/World.hpp>
#include <Unreal/UScriptStruct.hpp>

#include <algorithm>
#include <stdexcept>
#include <cmath>
#include <cstring>

namespace kh3ap::enemy_clear_detail
{
    int64_t unix_ms()
    {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
                   std::chrono::system_clock::now().time_since_epoch())
            .count();
    }

    std::string utf8(const wchar_t* name)
    {
        return to_utf8(name);
    }

    UClass* chest_notice_class()
    {
        auto* klass = UObjectGlobals::StaticFindObject<UClass*>(UClass::StaticClass(), nullptr,
                                                                chest_notice_class_path);
        return is_uobject_valid_fast(klass) ? klass : nullptr;
    }

    uint8_t reactor_command(UObject* reactor, const uint8_t* value)
    {
        if (!is_uobject_valid_fast(reactor))
        {
            throw std::runtime_error("invalid_chest_reactor");
        }
        auto* p = reactor->GetPropertyByNameInChain(L"m_Command");
        if (!p || p->GetElementSize() != 1 ||
            (p->GetClass().GetName() != L"EnumProperty" &&
             p->GetClass().GetName() != L"ByteProperty"))
        {
            throw std::runtime_error("chest_reactor_command_layout");
        }
        auto* storage = p->ContainerPtrToValuePtr<uint8_t>(reactor);
        if (value)
        {
            *storage = *value;
        }
        return *storage;
    }

    FMulticastScriptDelegate* chest_delegate(UObject* reactor)
    {
        if (!is_uobject_valid_fast(reactor))
        {
            throw std::runtime_error("invalid_chest_reactor");
        }
        auto* p = reactor->GetPropertyByNameInChain(L"OnReactorDoCommand");
        if (!p || !p->IsA<FMulticastDelegateProperty>() ||
            p->GetElementSize() != sizeof(FMulticastScriptDelegate))
        {
            throw std::runtime_error("chest_reactor_delegate_layout");
        }
        return p->ContainerPtrToValuePtr<FMulticastScriptDelegate>(reactor);
    }

    UObject* test_material()
    {
        return UObjectGlobals::StaticFindObject<UObject*>(nullptr, nullptr, test_material_path);
    }

    UObject* validated_test_material()
    {
        auto* source = test_material();
        auto* klass = UObjectGlobals::StaticFindObject<UClass*>(
            nullptr, nullptr, L"/Script/Engine.MaterialInterface");
        if (!is_uobject_valid_fast(source) || !is_uobject_valid_fast(klass) || !source->IsA(klass))
        {
            throw std::runtime_error("prepare_test_material_first");
        }
        return source;
    }

    std::array<float, 4> chest_hsb(UObject* material, const std::array<float, 4>* color)
    {
        Call call(material, color ? L"SetVectorParameterValue" : L"K2_GetVectorParameterValue");
        if (!write_name_or_string_function_param(call.field(L"ParameterName"), call.params.data(),
                                                 L"HSB"))
        {
            throw std::runtime_error("chest_color_parameter_layout");
        }
        auto* field = call.field(color ? L"Value" : L"ReturnValue");
        if (!field->IsA<FStructProperty>() ||
            field->GetElementSize() != sizeof(std::array<float, 4>) ||
            static_cast<FStructProperty*>(field)->GetStruct()->GetName() != L"LinearColor")
        {
            throw std::runtime_error("chest_linear_color_layout");
        }
        auto* storage = field->ContainerPtrToValuePtr<void>(call.params.data());
        if (color)
        {
            std::memcpy(storage, color->data(), sizeof(*color));
        }
        call.invoke();
        std::array<float, 4> result{};
        std::memcpy(result.data(), storage, sizeof(result));
        return result;
    }

    UObject* chest_diffuse(UObject* material)
    {
        Call get(material, L"K2_GetTextureParameterValue");
        if (!write_name_or_string_function_param(get.field(L"ParameterName"), get.params.data(),
                                                 L"T_Diffuse"))
        {
            throw std::runtime_error("chest_diffuse_parameter_layout");
        }
        get.invoke();
        return try_read_function_object_param(get.function, get.params.data(), L"ReturnValue");
    }

    bool keyblade_inventory(UObject* pawn, const std::wstring& token, bool grant, bool* dispatched)
    {
        // The caller supplies the current, validated Sora pawn, including for
        // the explicit test grant. Never use a library/world/game instance as context.
        auto* library = UObjectGlobals::StaticFindObject<UObject*>(
            nullptr, nullptr, L"/Script/TresGame.Default__TresGameBlueprintLibrary");
        Call call(library, grant ? L"BP_TresGetItem" : L"BP_TresHaveItem");
        if (!write_name_or_string_function_param(call.field(L"InItemKeyName"), call.params.data(),
                                                 token) ||
            !write_object_function_param(call.function, call.params.data(), L"WorldContextObject",
                                         pawn))
        {
            throw std::runtime_error("shooting_star_item_layout");
        }
        if (grant &&
            (!write_integral_function_param(call.field(L"InNum"), call.params.data(), 1) ||
             !write_bool_function_param(call.field(L"bShowInfo"), call.params.data(), true)))
        {
            throw std::runtime_error("shooting_star_grant_layout");
        }
        if (grant)
        {
            call.invoke();
            notify_runtime_keyblade_inventory_changed(token);
            if (dispatched)
            {
                *dispatched = true;
            }
            return keyblade_inventory(pawn, token);
        }
        auto* result = call.field(L"ReturnValue");
        if (result->GetClass().GetName() != L"BoolProperty")
        {
            throw std::runtime_error("item_result_layout");
        }
        call.invoke();
        return static_cast<FBoolProperty*>(result)->GetPropertyValueInContainer(call.params.data());
    }

    UObject* material_zero(UObject* mesh)
    {
        Call get(mesh, L"GetMaterial");
        if (!write_integral_function_param(get.field(L"ElementIndex"), get.params.data(), 0))
        {
            throw std::runtime_error("material_index_layout");
        }
        get.invoke();
        return try_read_function_object_param(get.function, get.params.data(), L"ReturnValue");
    }

    void set_material_zero(UObject* mesh, UObject* material)
    {
        Call set(mesh, L"SetMaterial");
        if (!write_integral_function_param(set.field(L"ElementIndex"), set.params.data(), 0) ||
            !write_object_function_param(set.function, set.params.data(), L"Material", material))
        {
            throw std::runtime_error("material_set_layout");
        }
        set.invoke();
    }

    int64_t integer(UObject* object, const wchar_t* name)
    {
        Call c(object, name);
        auto* p = c.field(L"ReturnValue");
        const auto kind = p->GetClass().GetName();
        if (kind != L"IntProperty" && kind != L"ByteProperty" && kind != L"EnumProperty")
        {
            throw std::runtime_error("invalid_integer_return");
        }
        c.invoke();
        auto result = read_function_int_param(c.function, c.params.data(), L"ReturnValue");
        if (!result)
        {
            throw std::runtime_error("unreadable_integer_return");
        }
        return *result;
    }

    bool boolean(UObject* object, const wchar_t* name)
    {
        Call c(object, name);
        auto* p = c.field(L"ReturnValue");
        if (p->GetClass().GetName() != L"BoolProperty")
        {
            throw std::runtime_error("invalid_bool_return");
        }
        c.invoke();
        return static_cast<FBoolProperty*>(p)->GetPropertyValueInContainer(c.params.data());
    }

    std::array<float, 3> position(UObject* object)
    {
        Call c(object, L"K2_GetActorLocation");
        auto* p = c.field(L"ReturnValue");
        if (p->GetClass().GetName() != L"StructProperty" ||
            p->GetElementSize() != sizeof(std::array<float, 3>) ||
            static_cast<FStructProperty*>(p)->GetStruct()->GetName() != L"Vector")
        {
            throw std::runtime_error("unsupported_vector_layout");
        }
        c.invoke();
        std::array<float, 3> value{};
        std::memcpy(value.data(), p->ContainerPtrToValuePtr<void>(c.params.data()),
                    sizeof(std::array<float, 3>));
        return value;
    }

    std::array<float, 3> rotation(UObject* object)
    {
        Call c(object, L"K2_GetActorRotation");
        auto* p = c.field(L"ReturnValue");
        if (p->GetClass().GetName() != L"StructProperty" ||
            p->GetElementSize() != sizeof(std::array<float, 3>) ||
            static_cast<FStructProperty*>(p)->GetStruct()->GetName() != L"Rotator")
        {
            throw std::runtime_error("unsupported_rotator_layout");
        }
        c.invoke();
        std::array<float, 3> value{};
        std::memcpy(value.data(), p->ContainerPtrToValuePtr<void>(c.params.data()),
                    sizeof(std::array<float, 3>));
        for (const auto component : value)
        {
            if (!std::isfinite(component))
            {
                throw std::runtime_error("nonfinite_actor_rotation");
            }
        }
        return value;
    }

    int32_t property_int(UObject* object, const wchar_t* name)
    {
        if (!is_uobject_valid_fast(object))
        {
            return -1;
        }
        auto* p = object->GetPropertyByNameInChain(name);
        if (!p || p->GetClass().GetName() != L"IntProperty" ||
            p->GetElementSize() != sizeof(int32_t))
        {
            return -1;
        }
        int32_t value{};
        std::memcpy(&value, p->ContainerPtrToValuePtr<void>(object), sizeof(value));
        return value;
    }

    std::wstring property_name(UObject* object, const wchar_t* name)
    {
        auto* p = object->GetPropertyByNameInChain(name);
        if (!p || p->GetClass().GetName() != L"NameProperty" ||
            p->GetElementSize() != sizeof(FName))
        {
            return {};
        }
        return p->ContainerPtrToValuePtr<FName>(object)->ToString();
    }

    bool property_bool(UObject* object, const wchar_t* name, bool fallback)
    {
        auto* p = object->GetPropertyByNameInChain(name);
        return p && p->GetClass().GetName() == L"BoolProperty"
                   ? static_cast<FBoolProperty*>(p)->GetPropertyValueInContainer(object)
                   : fallback;
    }

    UObject* actor_world(UObject* actor)
    {
        return is_uobject_valid_fast(actor) ? static_cast<AActor*>(actor)->GetWorld() : nullptr;
    }

    void json_position(std::ostream& out, const std::array<float, 3>& p)
    {
        out << '[' << p[0] << ',' << p[1] << ',' << p[2] << ']';
    }

    std::vector<AActor*> actors_of_class(UObject* pawn, UClass* klass)
    {
        // A broken engine wrapper must not look like an empty room.
        if (!is_uobject_valid_fast(klass) ||
            !UObjectGlobals::StaticFindObject<UFunction*>(
                nullptr, nullptr, L"/Script/Engine.GameplayStatics:GetAllActorsOfClass"))
        {
            throw std::runtime_error("actor_enumeration_unavailable");
        }
        std::vector<AActor*> actors;
        UGameplayStatics::GetAllActorsOfClass(pawn, klass, actors);
        return actors;
    }

    Call::Call(UObject* target, const wchar_t* name) : object(target)
    {
        if (!is_uobject_valid_fast(object))
        {
            throw std::runtime_error("invalid_call_target");
        }
        function = object->GetFunctionByNameInChain(name);
        if (!is_uobject_valid_fast(function) ||
            function->GetParmsSize() > max_enemy_command_parameter_bytes)
        {
            throw std::runtime_error("missing_function_or_layout:" + utf8(name));
        }
        params.resize(std::max<int>(1, function->GetParmsSize()));
    }

    FProperty* Call::field(const wchar_t* name)
    {
        auto* p = find_function_param_by_name(function, name);
        if (!p || p->GetOffset_Internal() < 0 ||
            p->GetOffset_Internal() + p->GetElementSize() > function->GetParmsSize())
        {
            throw std::runtime_error("missing_parameter_or_layout");
        }
        return p;
    }

    void Call::invoke(bool suppress_grant_observers)
    {
        if (suppress_grant_observers)
        {
            const RuntimeGrantInternalCallDepth::Scope scope{};
            object->ProcessEvent(function, params.data());
        }
        else
        {
            object->ProcessEvent(function, params.data());
        }
    }

} // namespace kh3ap::enemy_clear_detail
