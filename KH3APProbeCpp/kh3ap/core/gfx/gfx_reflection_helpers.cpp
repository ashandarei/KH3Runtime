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

    using gfx_detail::find_as_value_input_property;
    using gfx_detail::find_invoke_array_property;
    using gfx_detail::find_return_property;
    using gfx_detail::try_gfx_get_member_as_value;
    using gfx_detail::try_gfx_invoke_single_value;
    using gfx_detail::try_gfx_set_member_value_with_writer;

    namespace gfx_detail
    {

        auto find_return_property(UFunction* function_object) -> FProperty*
        {
            if (!function_object)
            {
                return nullptr;
            }
            for (FProperty* param = function_object->GetFirstProperty(); param;
                 param = param->HasNext() ? param->GetNextFieldAsProperty() : nullptr)
            {
                if (param && param->HasAnyPropertyFlags(CPF_ReturnParm))
                {
                    return param;
                }
            }
            return nullptr;
        }

        auto find_invoke_array_property(UFunction* function_object) -> FArrayProperty*
        {
            if (!function_object)
            {
                return nullptr;
            }
            FProperty* property = find_function_param_by_name(function_object, STR("Params"));
            if (!property || property->GetClass().GetName() != STR("ArrayProperty"))
            {
                for (FProperty* param = function_object->GetFirstProperty(); param;
                     param = param->HasNext() ? param->GetNextFieldAsProperty() : nullptr)
                {
                    if (param && param->HasAnyPropertyFlags(CPF_Parm) &&
                        !param->HasAnyPropertyFlags(CPF_OutParm) &&
                        !param->HasAnyPropertyFlags(CPF_ReturnParm) &&
                        param->GetClass().GetName() == STR("ArrayProperty"))
                    {
                        property = param;
                        break;
                    }
                }
            }
            return property && property->GetClass().GetName() == STR("ArrayProperty")
                       ? static_cast<FArrayProperty*>(property)
                       : nullptr;
        }

        auto try_gfx_set_member_value_with_writer(const GfxProbeFunctionPaths& paths,
                                                  UObject* object, std::wstring_view member_name,
                                                  const AsValueWriter& writer) -> bool
        {
            if (!object || !kh3ap::is_uobject_valid_fast(object) || member_name.empty() ||
                paths.set_member_value_path.empty() || !writer)
            {
                return false;
            }
            UFunction* const function_object =
                RC::Unreal::UObjectGlobals::StaticFindObject<UFunction*>(
                    nullptr, nullptr, std::wstring{paths.set_member_value_path}.c_str());
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
            FProperty* const value_property = find_as_value_input_property(function_object);
            void* const value_ptr =
                value_property ? value_property->ContainerPtrToValuePtr<void>(params.data())
                               : nullptr;
            if (!value_ptr || !writer(value_property, value_ptr))
            {
                return false;
            }
            object->ProcessEvent(function_object, params.data());
            return true;
        }

        auto try_gfx_get_member_as_value(const GfxProbeFunctionPaths& paths, UObject* object,
                                         std::wstring_view member_name,
                                         const std::function<void(void*, FProperty*)>& reader)
            -> bool
        {
            if (!object || !kh3ap::is_uobject_valid_fast(object) || member_name.empty() ||
                paths.get_member_value_path.empty() || !reader)
            {
                return false;
            }
            UFunction* const function_object =
                RC::Unreal::UObjectGlobals::StaticFindObject<UFunction*>(
                    nullptr, nullptr, std::wstring{paths.get_member_value_path}.c_str());
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
            object->ProcessEvent(function_object, params.data());
            FProperty* const return_property = find_return_property(function_object);
            void* const return_ptr =
                return_property ? return_property->ContainerPtrToValuePtr<void>(params.data())
                                : nullptr;
            if (!return_ptr)
            {
                return false;
            }
            reader(return_ptr, return_property);
            return true;
        }

        auto try_gfx_invoke_single_value(const GfxProbeFunctionPaths& paths, UObject* object,
                                         std::wstring_view function_name,
                                         const AsValueWriter& writer, UObject** return_object)
            -> bool
        {
            if (return_object)
            {
                *return_object = nullptr;
            }
            if (!object || !kh3ap::is_uobject_valid_fast(object) || function_name.empty() ||
                paths.invoke_path.empty())
            {
                return false;
            }
            UFunction* const function_object =
                RC::Unreal::UObjectGlobals::StaticFindObject<UFunction*>(
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
                    find_function_param_by_name(function_object, STR("FunctionName")),
                    params.data(), function_name))
            {
                return false;
            }

            FArrayProperty* const array_property = find_invoke_array_property(function_object);
            FProperty* const inner_property =
                array_property ? const_cast<FProperty*>(array_property->GetInner()) : nullptr;
            FProperty* const array_property_base = array_property;
            if (!array_property_base || !inner_property ||
                inner_property->GetClass().GetName() != STR("StructProperty"))
            {
                return false;
            }

            std::vector<std::byte> value_storage{};
            ArrayHeader* const header =
                array_property_base->ContainerPtrToValuePtr<ArrayHeader>(params.data());
            if (!header)
            {
                return false;
            }
            if (writer)
            {
                value_storage.resize(
                    static_cast<size_t>(std::max(inner_property->GetElementSize(), 0)));
                if (value_storage.empty())
                {
                    return false;
                }
                std::fill(value_storage.begin(), value_storage.end(), std::byte{0});
                if (!writer(inner_property, value_storage.data()))
                {
                    return false;
                }
                header->data_ptr = reinterpret_cast<uint64_t>(value_storage.data());
                header->num = 1;
                header->max = 1;
            }

            object->ProcessEvent(function_object, params.data());
            if (return_object)
            {
                FProperty* const return_property = find_return_property(function_object);
                void* const return_ptr =
                    return_property ? return_property->ContainerPtrToValuePtr<void>(params.data())
                                    : nullptr;
                *return_object =
                    return_ptr ? try_get_as_value_object(return_ptr, return_property) : nullptr;
            }
            return true;
        }
    } // namespace gfx_detail

    auto summarize_gfx_member_values(const GfxProbeFunctionPaths& paths, std::wstring_view label,
                                     UObject* object) -> std::wstring
    {
        if (!object || !kh3ap::is_uobject_valid_fast(object))
        {
            return STR("");
        }
        std::vector<std::wstring> parts{};
        parts.push_back(std::format(STR("{}={}"), label, format_object_debug_name(object)));
        for (std::wstring_view member_name : {
                 STR("_name"),
                 STR("name"),
                 STR("text"),
                 STR("htmlText"),
                 STR("label"),
                 STR("data"),
                 STR("value"),
                 STR("visible"),
                 STR("_visible"),
                 STR("alpha"),
                 STR("_alpha"),
                 STR("enabled"),
                 STR("selected"),
                 STR("focused"),
                 STR("state"),
                 STR("currentFrame"),
                 STR("currentLabel"),
                 STR("totalFrames"),
                 STR("numChildren"),
                 STR("x"),
                 STR("y"),
                 STR("scaleX"),
                 STR("scaleY"),
                 STR("width"),
                 STR("height"),
                 STR("index"),
                 STR("selectedIndex"),
                 STR("selectedItem"),
                 STR("dataProvider"),
                 STR("length"),
                 STR("rowCount"),
                 STR("scrollPosition"),
                 STR("_parent"),
                 STR("parent"),
                 STR("slot"),
                 STR("no"),
                 STR("num"),
             })
        {
            const std::wstring value = try_gfx_get_member_value(paths, object, member_name);
            if (value.find(STR("ASString=")) != std::wstring::npos ||
                value.find(STR("ASInt=")) != std::wstring::npos ||
                value.find(STR("ASNumber=")) != std::wstring::npos ||
                value.find(STR("ASBoolean=")) != std::wstring::npos ||
                value.find(STR("ASObject=")) != std::wstring::npos)
            {
                parts.push_back(
                    std::format(STR("{}={}"), member_name, truncate_for_log(value, 220)));
            }
        }
        return join_wide_parts(parts, STR(" ; "));
    }

} // namespace kh3ap
