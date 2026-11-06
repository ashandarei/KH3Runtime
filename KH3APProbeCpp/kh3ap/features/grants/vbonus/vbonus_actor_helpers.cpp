#include "kh3ap/features/grants/vbonus/vbonus_actor_helpers.h"

#include "kh3ap/core/objects/uobject_validity.h"
#include "kh3ap/features/grants/vbonus/translation_transform_layout.h"

#include <UnrealDef.hpp>
#include <Unreal/FString.hpp>
#include <Unreal/FProperty.hpp>
#include <Unreal/UObject.hpp>

#include <cstddef>
#include <cstring>
#include <string>

namespace kh3ap
{
    using namespace RC::Unreal;

    auto write_translation_transform_param(
            FProperty* property,
            void* params,
            double x,
            double y,
            double z) -> bool
    {
        if (!property || !params)
        {
            return false;
        }

        void* const value_ptr = property->ContainerPtrToValuePtr<void>(params);
        if (!value_ptr)
        {
            return false;
        }

        const int element_size = property->GetElementSize();
        if (element_size < static_cast<int>(translation_transform::storage_bytes))
        {
            return false;
        }
        return translation_transform::write(
            {static_cast<std::byte*>(value_ptr), static_cast<size_t>(element_size)}, x, y, z);
    }

    auto try_write_object_name_or_string_property(
            UObject* object,
            std::wstring_view property_name,
            std::wstring_view value) -> bool
    {
        if (!object || !kh3ap::is_uobject_valid_fast(object) || property_name.empty())
        {
            return false;
        }

        FProperty* const property = object->GetPropertyByNameInChain(std::wstring{property_name}.c_str());
        void* const value_ptr = object->GetValuePtrByPropertyNameInChain(std::wstring{property_name}.c_str());
        if (!property || !value_ptr)
        {
            return false;
        }

        const std::wstring property_class = property->GetClass().GetName();
        if (property_class == STR("NameProperty"))
        {
            *static_cast<FName*>(value_ptr) = FName(value, FNAME_Add);
            return true;
        }
        if (property_class == STR("StrProperty"))
        {
            *static_cast<FString*>(value_ptr) = FString(std::wstring{value}.c_str());
            return true;
        }
        return false;
    }

}
