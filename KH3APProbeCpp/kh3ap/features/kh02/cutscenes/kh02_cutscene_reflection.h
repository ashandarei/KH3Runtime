#pragma once
#include "kh3ap/features/kh02/core/kh02_reflected_call.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/bridge/utilities.h"
#include <Unreal/UObject.hpp>
#include <Unreal/FProperty.hpp>
#include <Unreal/Property/FBoolProperty.hpp>
#include <stdexcept>

namespace kh3ap::kh02cutscene_detail
{
    using namespace RC::Unreal;
    constexpr std::size_t reflected_parameter_limit_bytes = 2'048;

    struct ReflectedCall : Kh02ReflectedCall
    {
        ReflectedCall(UObject* owner, const wchar_t* method)
            : Kh02ReflectedCall(owner, method, "kh02_cutscene", reflected_parameter_limit_bytes)
        {
        }
    };

    inline bool flag(UObject* object, const wchar_t* property_name)
    {
        auto* property = is_uobject_valid_fast(object) ? object->GetPropertyByNameInChain(property_name) : nullptr;
        if (!property || !property->IsA<FBoolProperty>())
        {
            throw std::runtime_error("kh02_story_bool");
        }
        return static_cast<FBoolProperty*>(property)->GetPropertyValueInContainer(object);
    }

    inline float real(UObject* object, const wchar_t* property_name)
    {
        auto* property = is_uobject_valid_fast(object) ? object->GetPropertyByNameInChain(property_name) : nullptr;
        if (!property || property->GetClass().GetName() != L"FloatProperty" ||
            property->GetElementSize() != sizeof(float))
        {
            throw std::runtime_error("kh02_story_float");
        }
        return *property->ContainerPtrToValuePtr<float>(object);
    }

    inline void set_flag(UObject* object, const wchar_t* property_name, bool value)
    {
        if (!is_uobject_valid_fast(object) ||
            !write_bool_function_param(object->GetPropertyByNameInChain(property_name), object, value))
        {
            throw std::runtime_error("kh02_story_set_flag");
        }
    }

    inline std::string path(UObject* object)
    {
        return is_uobject_valid_fast(object) ? to_utf8(object->GetPathName()) : "";
    }

    template <class Operation> void restore_resource(Operation&& operation) noexcept
    {
        try
        {
            operation();
        }
        catch (...)
        {
        }
    }
} // namespace kh3ap::kh02cutscene_detail
