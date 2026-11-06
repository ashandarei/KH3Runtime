#pragma once
#include "kh3ap/features/kh02/core/kh02_reflected_call.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include <Unreal/UObject.hpp>
#include <Unreal/FProperty.hpp>
#include <Unreal/Property/FArrayProperty.hpp>
#include <stdexcept>

namespace kh3ap::kh02wardrobe_detail
{
    using namespace RC::Unreal;
    constexpr std::size_t reflected_parameter_limit_bytes = 512;
    constexpr int maximum_asset_array_entries = 64;

    struct Call : Kh02ReflectedCall
    {
        Call(UObject* target, const wchar_t* method)
            : Kh02ReflectedCall(target, method, "0.2 wardrobe", reflected_parameter_limit_bytes)
        {
        }

        Call(const wchar_t* target, const wchar_t* method)
            : Kh02ReflectedCall(target, method, "0.2 wardrobe", reflected_parameter_limit_bytes)
        {
        }
    };

    constexpr auto gameplay = L"/Script/Engine.Default__GameplayStatics";

    inline auto object_field(UObject* owner, const wchar_t* name) -> UObject*
    {
        return is_uobject_valid_fast(owner) ? try_read_reflected_object_property_value(owner, name) : nullptr;
    }

    struct Array
    {
        void* data;
        int32_t count;
        int32_t capacity;
    };

    inline auto array_field(UObject* object, const wchar_t* name) -> std::pair<Array*, FProperty*>
    {
        auto* property = is_uobject_valid_fast(object) ? object->GetPropertyByNameInChain(name) : nullptr;
        if (!property || !property->IsA<FArrayProperty>() || property->GetElementSize() != sizeof(Array))
        {
            throw std::runtime_error("Missing wardrobe asset array");
        }
        auto* array = property->ContainerPtrToValuePtr<Array>(object);
        if (array->count < 0 || array->count > maximum_asset_array_entries || array->capacity < array->count ||
            (array->count && !array->data))
        {
            throw std::runtime_error("Invalid wardrobe asset array");
        }
        return {array, static_cast<FArrayProperty*>(property)->GetInner()};
    }

} // namespace kh3ap::kh02wardrobe_detail
