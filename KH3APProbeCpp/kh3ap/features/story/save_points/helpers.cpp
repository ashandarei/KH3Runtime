#include "kh3ap/features/story/save_points/helpers.h"
#include "kh3ap/features/story/save_points/text.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/core/objects/uobject_validity.h"
#include "kh3ap/features/grants/grant_internal_call_depth.h"
#include <UnrealDef.hpp>
#include <Unreal/FProperty.hpp>
#include <Unreal/FWeakObjectPtr.hpp>
#include <Unreal/Property/FBoolProperty.hpp>
#include <Unreal/UClass.hpp>
#include <Unreal/UFunction.hpp>
#include <Unreal/UObject.hpp>
#include <Unreal/UObjectGlobals.hpp>
#include <algorithm>
#include <chrono>
#include <cstring>
#include <format>
#include <unordered_map>
#include <utility>

namespace kh3ap::save_point_native
{
    using namespace RC::Unreal;
    using save_point_text::bool_text;
    using save_point_text::object_lookup_candidates;
    auto try_read_object_property_trusted(UObject* object, std::wstring_view property_name)
        -> UObject*
    {
        if (!object || property_name.empty())
        {
            return nullptr;
        }

        const std::wstring owned_property_name{property_name};
        FProperty* const property = object->GetPropertyByNameInChain(owned_property_name.c_str());
        void* const value_ptr =
            object->GetValuePtrByPropertyNameInChain(owned_property_name.c_str());
        if (!property || !value_ptr || property->GetClass().GetName() != STR("ObjectProperty"))
        {
            return nullptr;
        }

        UObject* value = nullptr;
        std::memcpy(&value, value_ptr, sizeof(value));
        return value;
    }

    auto try_read_object_property(UObject* object, std::wstring_view property_name) -> UObject*
    {
        if (!object || !kh3ap::is_uobject_valid_fast(object))
        {
            return nullptr;
        }
        return try_read_object_property_trusted(object, property_name);
    }

    auto describe_object_property(UObject* object, std::wstring_view property_name) -> std::wstring
    {
        if (!object || !kh3ap::is_uobject_valid_fast(object))
        {
            return STR("<invalid actor>");
        }
        const std::wstring owned_property_name{property_name};
        FProperty* const property = object->GetPropertyByNameInChain(owned_property_name.c_str());
        if (!property)
        {
            return STR("<missing property>");
        }
        if (property->GetClass().GetName() != STR("ObjectProperty"))
        {
            return std::format(STR("<{}>"), property->GetClass().GetName());
        }
        UObject* const value = try_read_object_property(object, property_name);
        return value && kh3ap::is_uobject_valid_fast(value) ? format_object_debug_name(value)
                                                            : STR("<null>");
    }

    auto try_resolve_object_path(std::wstring_view raw_path, std::wstring* lookup_path) -> UObject*
    {
        for (const std::wstring& candidate : object_lookup_candidates(raw_path))
        {
            UObject* const object =
                UObjectGlobals::StaticFindObject<UObject*>(nullptr, nullptr, candidate.c_str());
            if (object && kh3ap::is_uobject_valid_fast(object))
            {
                if (lookup_path)
                {
                    *lookup_path = candidate;
                }
                return object;
            }
        }
        return nullptr;
    }

    auto try_write_object_property(UObject* object, std::wstring_view property_name, UObject* value,
                                   std::wstring* reason) -> bool
    {
        if (!object || !kh3ap::is_uobject_valid_fast(object))
        {
            if (reason)
            {
                *reason = STR("invalid_actor");
            }
            return false;
        }
        if (!value || !kh3ap::is_uobject_valid_fast(value))
        {
            if (reason)
            {
                *reason = STR("invalid_value");
            }
            return false;
        }

        const std::wstring owned_property_name{property_name};
        FProperty* const property = object->GetPropertyByNameInChain(owned_property_name.c_str());
        void* const value_ptr =
            object->GetValuePtrByPropertyNameInChain(owned_property_name.c_str());
        if (!property || !value_ptr)
        {
            if (reason)
            {
                *reason = STR("missing_property");
            }
            return false;
        }
        if (property->GetClass().GetName() != STR("ObjectProperty"))
        {
            if (reason)
            {
                *reason =
                    std::format(STR("non_object_property:{}"), property->GetClass().GetName());
            }
            return false;
        }

        std::memcpy(value_ptr, &value, sizeof(value));
        return true;
    }

    auto format_scalar_property_value(UObject* object, std::wstring_view property_name)
        -> std::wstring
    {
        if (!object || !kh3ap::is_uobject_valid_fast(object))
        {
            return STR("<invalid actor>");
        }

        const std::wstring owned_property_name{property_name};
        FProperty* const property = object->GetPropertyByNameInChain(owned_property_name.c_str());
        if (!property)
        {
            return STR("<missing>");
        }

        const std::wstring property_class = property->GetClass().GetName();
        if (property_class == STR("BoolProperty"))
        {
            auto* const bool_property = static_cast<FBoolProperty*>(property);
            return bool_text(bool_property->GetPropertyValueInContainer(object));
        }
        if (property_class == STR("NameProperty"))
        {
            return try_read_object_name_property(object, owned_property_name.c_str())
                .value_or(STR("<unread_name>"));
        }

        return try_read_object_scalar_property_as_string(object, owned_property_name.c_str())
            .value_or(STR("<unread>"));
    }

    auto try_write_object_function_param(FProperty* property, void* params, UObject* value) -> bool
    {
        if (!property || !params)
        {
            return false;
        }
        UObject** const object_value = property->ContainerPtrToValuePtr<UObject*>(params);
        if (!object_value)
        {
            return false;
        }
        *object_value = value;
        return true;
    }

    auto try_write_class_function_param(FProperty* property, void* params, UClass* value) -> bool
    {
        if (!property || !params || !value)
        {
            return false;
        }
        UClass** const class_value = property->ContainerPtrToValuePtr<UClass*>(params);
        if (!class_value)
        {
            return false;
        }
        *class_value = value;
        return true;
    }

    auto try_read_object_return_value(UFunction* function_object, void* params) -> UObject*
    {
        if (!function_object || !params)
        {
            return nullptr;
        }
        FProperty* const return_property =
            find_function_param_by_name(function_object, STR("ReturnValue"));
        if (!return_property)
        {
            return nullptr;
        }
        UObject** const return_value = return_property->ContainerPtrToValuePtr<UObject*>(params);
        return return_value && *return_value && kh3ap::is_uobject_valid_fast(*return_value)
                   ? *return_value
                   : nullptr;
    }
} // namespace kh3ap::save_point_native
