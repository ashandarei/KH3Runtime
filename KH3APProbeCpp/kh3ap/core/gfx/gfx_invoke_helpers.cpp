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
#include <algorithm>
#include <cstring>
#include <format>
#include <string>
#include <vector>

#include <Unreal/UObjectGlobals.hpp>
#include <array>
#include <functional>

namespace kh3ap
{
    namespace
    {
        constexpr auto max_inspected_gfx_array_index = 32;
    }

    using gfx_detail::find_invoke_array_property;
    using gfx_detail::try_gfx_invoke_single_value;
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

    auto try_gfx_invoke_get_child_at(const GfxProbeFunctionPaths& paths, UObject* object,
                                     int32_t index) -> UObject*
    {
        if (!object || !kh3ap::is_uobject_valid_fast(object) || index < 0 || index > max_inspected_gfx_array_index ||
            paths.invoke_path.empty())
        {
            return nullptr;
        }
        const std::wstring invoke_path{paths.invoke_path};
        UFunction* const function_object = RC::Unreal::UObjectGlobals::StaticFindObject<UFunction*>(
            nullptr, nullptr, invoke_path.c_str());
        if (!function_object)
        {
            return nullptr;
        }

        std::vector<std::byte> params(static_cast<size_t>(function_object->GetParmsSize()));
        if (params.empty())
        {
            return nullptr;
        }
        std::fill(params.begin(), params.end(), std::byte{0});

        if (!write_name_or_string_function_param(
                find_function_param_by_name(function_object, STR("FunctionName")), params.data(),
                STR("getChildAt")))
        {
            return nullptr;
        }

        FProperty* array_property_base =
            find_function_param_by_name(function_object, STR("Params"));
        if (!array_property_base ||
            array_property_base->GetClass().GetName() != STR("ArrayProperty"))
        {
            for (FProperty* param = function_object->GetFirstProperty(); param;
                 param = param->HasNext() ? param->GetNextFieldAsProperty() : nullptr)
            {
                if (param && param->HasAnyPropertyFlags(CPF_Parm) &&
                    !param->HasAnyPropertyFlags(CPF_OutParm) &&
                    !param->HasAnyPropertyFlags(CPF_ReturnParm) &&
                    param->GetClass().GetName() == STR("ArrayProperty"))
                {
                    array_property_base = param;
                    break;
                }
            }
        }
        auto* const array_property =
            array_property_base && array_property_base->GetClass().GetName() == STR("ArrayProperty")
                ? static_cast<FArrayProperty*>(array_property_base)
                : nullptr;
        FProperty* const inner_property =
            array_property ? const_cast<FProperty*>(array_property->GetInner()) : nullptr;
        if (!array_property_base || !inner_property ||
            inner_property->GetClass().GetName() != STR("StructProperty"))
        {
            return nullptr;
        }

        std::vector<std::byte> value_storage(
            static_cast<size_t>(std::max(inner_property->GetElementSize(), 0)));
        if (value_storage.empty())
        {
            return nullptr;
        }
        std::fill(value_storage.begin(), value_storage.end(), std::byte{0});
        if (!write_as_value_int(inner_property, value_storage.data(), index))
        {
            return nullptr;
        }

        if (ArrayHeader* const header =
                array_property_base->ContainerPtrToValuePtr<ArrayHeader>(params.data()))
        {
            header->data_ptr = reinterpret_cast<uint64_t>(value_storage.data());
            header->num = 1;
            header->max = 1;
        }
        else
        {
            return nullptr;
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
            return nullptr;
        }
        void* const return_ptr = return_property->ContainerPtrToValuePtr<void>(params.data());
        return try_get_as_value_object(return_ptr, return_property);
    }

    auto try_gfx_invoke_no_args(const GfxProbeFunctionPaths& paths, UObject* object,
                                std::wstring_view function_name) -> bool
    {
        return try_gfx_invoke_single_value(paths, object, function_name, {}, nullptr);
    }

    auto try_gfx_invoke_no_args_object(const GfxProbeFunctionPaths& paths, UObject* object,
                                       std::wstring_view function_name) -> UObject*
    {
        UObject* result = nullptr;
        if (!try_gfx_invoke_single_value(paths, object, function_name, {}, &result))
        {
            return nullptr;
        }
        return result && kh3ap::is_uobject_valid_fast(result) ? result : nullptr;
    }

    auto try_gfx_invoke_string_arg_object(const GfxProbeFunctionPaths& paths, UObject* object,
                                          std::wstring_view function_name, std::wstring_view value)
        -> UObject*
    {
        UObject* result = nullptr;
        if (value.empty() || !try_gfx_invoke_single_value(
                                 paths, object, function_name,
                                 [value](FProperty* property, void* value_ptr)
                                 {
                                     return write_as_value_string(property, value_ptr, value);
                                 },
                                 &result))
        {
            return nullptr;
        }
        return result && kh3ap::is_uobject_valid_fast(result) ? result : nullptr;
    }

    auto try_gfx_invoke_int_arg(const GfxProbeFunctionPaths& paths, UObject* object,
                                std::wstring_view function_name, int32_t value) -> bool
    {
        return try_gfx_invoke_single_value(
            paths, object, function_name,
            [value](FProperty* property, void* value_ptr)
            {
                return write_as_value_int(property, value_ptr, value);
            },
            nullptr);
    }

    auto try_gfx_invoke_int_int_number_args(const GfxProbeFunctionPaths& paths, UObject* object,
                                            std::wstring_view function_name, int32_t first,
                                            int32_t second, double third) -> bool
    {
        if (!object || !kh3ap::is_uobject_valid_fast(object) || function_name.empty() ||
            paths.invoke_path.empty())
        {
            return false;
        }
        UFunction* const function_object = RC::Unreal::UObjectGlobals::StaticFindObject<UFunction*>(
            nullptr, nullptr, std::wstring{paths.invoke_path}.c_str());
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
                find_function_param_by_name(function_object, STR("FunctionName")), params.data(),
                function_name))
        {
            return false;
        }

        FArrayProperty* const array_property = find_invoke_array_property(function_object);
        FProperty* const inner_property =
            array_property ? const_cast<FProperty*>(array_property->GetInner()) : nullptr;
        const int32_t element_size = inner_property ? inner_property->GetElementSize() : 0;
        if (!array_property || !inner_property || element_size <= 0 ||
            inner_property->GetClass().GetName() != STR("StructProperty"))
        {
            return false;
        }

        constexpr int32_t argument_count = 3;
        std::vector<std::byte> value_storage(static_cast<size_t>(element_size) * argument_count);
        std::fill(value_storage.begin(), value_storage.end(), std::byte{0});
        void* const first_ptr = value_storage.data();
        void* const second_ptr = value_storage.data() + element_size;
        void* const third_ptr = value_storage.data() + (element_size * 2);
        if (!write_as_value_int(inner_property, first_ptr, first) ||
            !write_as_value_int(inner_property, second_ptr, second) ||
            !write_as_value_number(inner_property, third_ptr, third))
        {
            return false;
        }

        ArrayHeader* const header =
            array_property->ContainerPtrToValuePtr<ArrayHeader>(params.data());
        if (!header)
        {
            return false;
        }
        header->data_ptr = reinterpret_cast<uint64_t>(value_storage.data());
        header->num = argument_count;
        header->max = argument_count;

        object->ProcessEvent(function_object, params.data());
        return true;
    }

    auto try_gfx_invoke_object_arg(const GfxProbeFunctionPaths& paths, UObject* object,
                                   std::wstring_view function_name, UObject* value) -> bool
    {
        return try_gfx_invoke_single_value(
            paths, object, function_name,
            [value](FProperty* property, void* value_ptr)
            {
                return write_as_value_object(property, value_ptr, value);
            },
            nullptr);
    }

    auto try_gfx_invoke_replace_text(const GfxProbeFunctionPaths& paths, UObject* object,
                                     std::wstring_view value) -> bool
    {
        if (!object || !kh3ap::is_uobject_valid_fast(object) || value.empty() ||
            paths.invoke_path.empty())
        {
            return false;
        }
        const std::wstring invoke_path{paths.invoke_path};
        UFunction* const function_object = RC::Unreal::UObjectGlobals::StaticFindObject<UFunction*>(
            nullptr, nullptr, invoke_path.c_str());
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
                find_function_param_by_name(function_object, STR("FunctionName")), params.data(),
                STR("replaceText")))
        {
            return false;
        }

        FProperty* array_property_base =
            find_function_param_by_name(function_object, STR("Params"));
        if (!array_property_base ||
            array_property_base->GetClass().GetName() != STR("ArrayProperty"))
        {
            for (FProperty* param = function_object->GetFirstProperty(); param;
                 param = param->HasNext() ? param->GetNextFieldAsProperty() : nullptr)
            {
                if (param && param->HasAnyPropertyFlags(CPF_Parm) &&
                    !param->HasAnyPropertyFlags(CPF_OutParm) &&
                    !param->HasAnyPropertyFlags(CPF_ReturnParm) &&
                    param->GetClass().GetName() == STR("ArrayProperty"))
                {
                    array_property_base = param;
                    break;
                }
            }
        }
        auto* const array_property =
            array_property_base && array_property_base->GetClass().GetName() == STR("ArrayProperty")
                ? static_cast<FArrayProperty*>(array_property_base)
                : nullptr;
        FProperty* const inner_property =
            array_property ? const_cast<FProperty*>(array_property->GetInner()) : nullptr;
        const int element_size = inner_property ? inner_property->GetElementSize() : 0;
        if (!array_property_base || !inner_property ||
            inner_property->GetClass().GetName() != STR("StructProperty") || element_size <= 0)
        {
            return false;
        }

        constexpr int32_t argument_count = 3;
        std::vector<std::byte> value_storage(static_cast<size_t>(element_size * argument_count));
        std::fill(value_storage.begin(), value_storage.end(), std::byte{0});
        void* const begin_ptr = value_storage.data();
        void* const end_ptr = value_storage.data() + element_size;
        void* const text_ptr = value_storage.data() + (element_size * 2);
        if (!write_as_value_int(inner_property, begin_ptr, 0) ||
            !write_as_value_int(inner_property, end_ptr, 9999) ||
            !write_as_value_string(inner_property, text_ptr, value))
        {
            return false;
        }

        if (ArrayHeader* const header =
                array_property_base->ContainerPtrToValuePtr<ArrayHeader>(params.data()))
        {
            header->data_ptr = reinterpret_cast<uint64_t>(value_storage.data());
            header->num = argument_count;
            header->max = argument_count;
        }
        else
        {
            return false;
        }

        object->ProcessEvent(function_object, params.data());
        return true;
    }

    auto try_gfx_child_path(const GfxProbeFunctionPaths& paths, UObject* object,
                            std::initializer_list<int32_t> path) -> UObject*
    {
        UObject* current = object;
        for (const int32_t index : path)
        {
            current = try_gfx_invoke_get_child_at(paths, current, index);
            if (!current || !kh3ap::is_uobject_valid_fast(current))
            {
                return nullptr;
            }
        }
        return current;
    }

    auto summarize_gfx_child_member_values(const GfxProbeFunctionPaths& paths,
                                           std::wstring_view label, UObject* object, int depth)
        -> std::wstring
    {
        if (!object || !kh3ap::is_uobject_valid_fast(object))
        {
            return STR("");
        }
        if (depth >= 5)
        {
            return STR("");
        }

        std::vector<std::wstring> parts{};
        for (int32_t index = 0; index < 8; ++index)
        {
            UObject* const child = try_gfx_invoke_get_child_at(paths, object, index);
            if (!child || !kh3ap::is_uobject_valid_fast(child))
            {
                continue;
            }
            const std::wstring child_label = std::format(STR("{}.child{}"), label, index);
            std::wstring child_summary = summarize_gfx_member_values(paths, child_label, child);
            if (!child_summary.empty())
            {
                parts.push_back(std::move(child_summary));
            }
            std::wstring nested_summary =
                summarize_gfx_child_member_values(paths, child_label, child, depth + 1);
            if (!nested_summary.empty())
            {
                parts.push_back(std::move(nested_summary));
            }
        }

        return parts.empty() ? std::wstring{} : join_wide_parts(parts, STR(" || "));
    }
} // namespace kh3ap
