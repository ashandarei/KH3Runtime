#include "kh3ap/core/gfx/as_value_type.h"
#include "kh3ap/core/gfx/gfx_reflection_helpers.h"
#include "kh3ap/core/gfx/gfx_reflection_internal.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/bridge/utilities.h"
#include <UnrealDef.hpp>
#include <Unreal/FProperty.hpp>
#include <Unreal/FString.hpp>
#include <Unreal/FText.hpp>
#include <Unreal/NameTypes.hpp>
#include <Unreal/Property/FArrayProperty.hpp>
#include <Unreal/Property/FBoolProperty.hpp>
#include <Unreal/Property/FStructProperty.hpp>
#include <Unreal/UScriptStruct.hpp>
#include <Unreal/UClass.hpp>
#include <Unreal/UFunction.hpp>
#include <Unreal/UObject.hpp>
#include <Unreal/UObjectGlobals.hpp>
#include <algorithm>
#include <cstring>
#include <format>
#include <string>
#include <vector>

namespace kh3ap
{
    using gfx_detail::try_gfx_get_member_as_value;
    using gfx_detail::try_gfx_set_member_value_with_writer;
    using RC::Unreal::CPF_OutParm;
    using RC::Unreal::CPF_Parm;
    using RC::Unreal::CPF_ReturnParm;
    using RC::Unreal::FArrayProperty;
    using RC::Unreal::FProperty;
    using RC::Unreal::FString;
    using RC::Unreal::FStructProperty;
    using RC::Unreal::UFunction;
    using RC::Unreal::UObject;
    using RC::Unreal::UScriptStruct;

    auto try_gfx_get_member_value(const GfxProbeFunctionPaths& paths, UObject* object,
                                  std::wstring_view member_name) -> std::wstring
    {
        if (!object || !kh3ap::is_uobject_valid_fast(object) || member_name.empty())
        {
            return STR("<none>");
        }
        if (paths.get_member_value_path.empty())
        {
            return STR("<missing-function>");
        }
        const std::wstring get_member_value_path{paths.get_member_value_path};
        UFunction* const function_object = RC::Unreal::UObjectGlobals::StaticFindObject<UFunction*>(
            nullptr, nullptr, get_member_value_path.c_str());
        if (!function_object)
        {
            return STR("<missing-function>");
        }

        std::vector<std::byte> params(static_cast<size_t>(function_object->GetParmsSize()));
        if (params.empty())
        {
            return STR("<missing-params>");
        }
        std::fill(params.begin(), params.end(), std::byte{0});

        FProperty* member_property =
            find_function_param_by_name(function_object, STR("MemberName"));
        if (!write_name_or_string_function_param(member_property, params.data(), member_name))
        {
            return STR("<write-failed>");
        }

        object->ProcessEvent(function_object, params.data());

        FProperty* return_property = nullptr;
        for (FProperty* param = function_object->GetFirstProperty(); param;
             param = param->HasNext() ? param->GetNextFieldAsProperty() : nullptr)
        {
            if (param && param->HasAnyPropertyFlags(CPF_ReturnParm))
            {
                return_property = param;
                break;
            }
        }
        if (!return_property)
        {
            return STR("<missing-return>");
        }
        void* const return_ptr = return_property->ContainerPtrToValuePtr<void>(params.data());
        return summarize_as_value_struct(return_ptr, return_property);
    }

    auto try_gfx_get_member_value_object(const GfxProbeFunctionPaths& paths, UObject* object,
                                         std::wstring_view member_name) -> UObject*
    {
        UObject* result = nullptr;
        (void)try_gfx_get_member_as_value(paths, object, member_name,
                                          [&result](void* value_ptr, FProperty* value_property)
                                          {
                                              result = try_get_as_value_object(value_ptr,
                                                                               value_property);
                                          });
        return result && kh3ap::is_uobject_valid_fast(result) ? result : nullptr;
    }

    auto try_gfx_get_member_value_number(const GfxProbeFunctionPaths& paths, UObject* object,
                                         std::wstring_view member_name) -> std::optional<double>
    {
        std::optional<double> result{};
        (void)try_gfx_get_member_as_value(paths, object, member_name,
                                          [&result](void* value_ptr, FProperty* value_property)
                                          {
                                              result = try_get_as_value_number(value_ptr,
                                                                               value_property);
                                          });
        return result;
    }

    auto try_gfx_get_member_value_string(const GfxProbeFunctionPaths& paths, UObject* object,
                                         std::wstring_view member_name)
        -> std::optional<std::wstring>
    {
        std::optional<std::wstring> result{};
        (void)try_gfx_get_member_as_value(paths, object, member_name,
                                          [&result](void* value_ptr, FProperty* value_property)
                                          {
                                              result = try_get_as_value_string(value_ptr,
                                                                               value_property);
                                          });
        return result;
    }

    auto try_gfx_set_member_value_string(const GfxProbeFunctionPaths& paths, UObject* object,
                                         std::wstring_view member_name, std::wstring_view value)
        -> bool
    {
        if (!object || !kh3ap::is_uobject_valid_fast(object) || member_name.empty() ||
            value.empty() || paths.set_member_value_path.empty())
        {
            return false;
        }
        const std::wstring set_member_value_path{paths.set_member_value_path};
        UFunction* const function_object = RC::Unreal::UObjectGlobals::StaticFindObject<UFunction*>(
            nullptr, nullptr, set_member_value_path.c_str());
        if (!function_object)
        {
            return false;
        }

        std::vector<std::byte> params(static_cast<size_t>(function_object->GetParmsSize()));
        if (params.empty())
        {
            return false;
        }
        std::fill(params.begin(), params.end(), std::byte{0});

        if (!write_name_or_string_function_param(
                find_function_param_by_name(function_object, STR("MemberName")), params.data(),
                member_name))
        {
            return false;
        }

        FProperty* value_property = find_function_param_by_name(function_object, STR("Input"));
        if (!value_property)
        {
            value_property = find_function_param_by_name(function_object, STR("Value"));
        }
        if (!value_property || value_property->GetClass().GetName() != STR("StructProperty"))
        {
            for (FProperty* param = function_object->GetFirstProperty(); param;
                 param = param->HasNext() ? param->GetNextFieldAsProperty() : nullptr)
            {
                if (param && param->HasAnyPropertyFlags(CPF_Parm) &&
                    !param->HasAnyPropertyFlags(CPF_OutParm) &&
                    !param->HasAnyPropertyFlags(CPF_ReturnParm) &&
                    param->GetClass().GetName() == STR("StructProperty"))
                {
                    value_property = param;
                    break;
                }
            }
        }
        if (!value_property)
        {
            return false;
        }
        void* const value_ptr = value_property->ContainerPtrToValuePtr<void>(params.data());
        if (!write_as_value_string(value_property, value_ptr, value))
        {
            return false;
        }

        object->ProcessEvent(function_object, params.data());
        return true;
    }

    auto try_gfx_set_member_value_int(const GfxProbeFunctionPaths& paths, UObject* object,
                                      std::wstring_view member_name, int32_t value) -> bool
    {
        return try_gfx_set_member_value_with_writer(paths, object, member_name,
                                                    [value](FProperty* property, void* value_ptr)
                                                    {
                                                        return write_as_value_int(property,
                                                                                  value_ptr, value);
                                                    });
    }

    auto try_gfx_set_member_value_bool(const GfxProbeFunctionPaths& paths, UObject* object,
                                       std::wstring_view member_name, bool value) -> bool
    {
        return try_gfx_set_member_value_with_writer(paths, object, member_name,
                                                    [value](FProperty* property, void* value_ptr)
                                                    {
                                                        return write_as_value_bool(
                                                            property, value_ptr, value);
                                                    });
    }

    auto try_gfx_set_member_value_number(const GfxProbeFunctionPaths& paths, UObject* object,
                                         std::wstring_view member_name, double value) -> bool
    {
        return try_gfx_set_member_value_with_writer(paths, object, member_name,
                                                    [value](FProperty* property, void* value_ptr)
                                                    {
                                                        return write_as_value_number(
                                                            property, value_ptr, value);
                                                    });
    }

    auto try_gfx_set_member_value_object(const GfxProbeFunctionPaths& paths, UObject* object,
                                         std::wstring_view member_name, UObject* value) -> bool
    {
        return try_gfx_set_member_value_with_writer(paths, object, member_name,
                                                    [value](FProperty* property, void* value_ptr)
                                                    {
                                                        return write_as_value_object(
                                                            property, value_ptr, value);
                                                    });
    }

    auto try_gfx_set_member_value_null(const GfxProbeFunctionPaths& paths, UObject* object,
                                       std::wstring_view member_name) -> bool
    {
        return try_gfx_set_member_value_with_writer(
            paths, object, member_name,
            [](FProperty* property, void* value_ptr)
            {
                if (!property || !value_ptr ||
                    property->GetClass().GetName() != STR("StructProperty"))
                {
                    return false;
                }
                auto* structure = static_cast<FStructProperty*>(property)->GetStruct();
                auto* type =
                    structure ? find_struct_field_by_name(structure, STR("ASType")) : nullptr;
                if (!type)
                {
                    return false;
                }
                std::memset(value_ptr, 0, static_cast<size_t>(property->GetElementSize()));
                *type->ContainerPtrToValuePtr<uint8_t>(value_ptr) =
                    as_value_type_id(ASValueType::Null);
                return true;
            });
    }
} // namespace kh3ap
