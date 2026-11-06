#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/bridge/utilities.h"

#include <UnrealDef.hpp>
#include <Unreal/FProperty.hpp>
#include <Unreal/Property/FArrayProperty.hpp>
#include <Unreal/Property/FStructProperty.hpp>
#include <Unreal/UScriptStruct.hpp>
#include <Unreal/UClass.hpp>
#include <Unreal/UFunction.hpp>
#include <Unreal/UObject.hpp>
#include <algorithm>
#include <cstdint>
#include <format>
#include <sstream>
#include <string>
#include <vector>

namespace kh3ap
{
    namespace
    {
        constexpr uintptr_t minimum_object_address = 0x10000;
        constexpr int scalar_preview_byte_count = sizeof(uint64_t);
    } // namespace

    auto summarize_param_flags(RC::Unreal::FProperty* property) -> std::wstring
    {
        if (!property)
        {
            return STR("<null>");
        }

        std::wstring flags{};
        if (property->HasAnyPropertyFlags(RC::Unreal::CPF_Parm))
        {
            flags += STR("Parm|");
        }
        if (property->HasAnyPropertyFlags(RC::Unreal::CPF_OutParm))
        {
            flags += STR("Out|");
        }
        if (property->HasAnyPropertyFlags(RC::Unreal::CPF_ReturnParm))
        {
            flags += STR("Return|");
        }
        if (property->HasAnyPropertyFlags(RC::Unreal::CPF_ReferenceParm))
        {
            flags += STR("Ref|");
        }
        if (flags.empty())
        {
            return STR("None");
        }
        flags.pop_back();
        return flags;
    }

    auto describe_property_detail(RC::Unreal::FProperty* property) -> std::wstring
    {
        if (!property)
        {
            return STR("<null>");
        }

        const std::wstring property_class = property->GetClass().GetName();
        if (property_class == STR("StructProperty"))
        {
            auto* struct_property = static_cast<RC::Unreal::FStructProperty*>(property);
            RC::Unreal::UScriptStruct* script_struct =
                struct_property ? struct_property->GetStruct() : nullptr;
            return std::format(STR("struct={}"), script_struct ? script_struct->GetFullName()
                                                               : std::wstring{STR("<null>")});
        }
        if (property_class == STR("ArrayProperty"))
        {
            auto* array_property = static_cast<RC::Unreal::FArrayProperty*>(property);
            RC::Unreal::FProperty* inner_property =
                array_property ? const_cast<RC::Unreal::FProperty*>(array_property->GetInner())
                               : nullptr;
            if (!inner_property)
            {
                return STR("inner=<null>");
            }
            return std::format(STR("inner_class={} inner_name={} inner_size={}"),
                               inner_property->GetClass().GetName(), inner_property->GetName(),
                               inner_property->GetElementSize());
        }
        return STR("-");
    }

    auto summarize_stable_function_params(RC::Unreal::UFunction* function_object, void* params,
                                          bool include_returns) -> std::wstring
    {
        if (!function_object || !params)
        {
            return STR("<none>");
        }

        std::vector<std::wstring> param_parts{};
        for (RC::Unreal::FProperty* param = function_object->GetFirstProperty(); param;
             param = param->HasNext() ? param->GetNextFieldAsProperty() : nullptr)
        {
            if (!param->HasAnyPropertyFlags(RC::Unreal::CPF_Parm))
            {
                continue;
            }

            const bool is_return_param = param->HasAnyPropertyFlags(RC::Unreal::CPF_ReturnParm);
            if (is_return_param && !include_returns)
            {
                continue;
            }

            const int offset = param->GetOffset_Internal();
            const int element_size = param->GetElementSize();
            if (offset < 0 || element_size <= 0)
            {
                continue;
            }

            const auto* params_bytes = static_cast<const std::byte*>(params);
            const void* value_ptr = params_bytes + offset;
            param_parts.emplace_back(std::format(
                STR("{}{}:{}={}[raw={}]"), is_return_param ? STR("return ") : STR(""),
                param->GetName(), param->GetClass().GetName(),
                format_stable_ability_trace_value(param, value_ptr, element_size, param->GetName()),
                format_bytes_hex(value_ptr, static_cast<size_t>(std::clamp(
                                                element_size, 1, scalar_preview_byte_count)))));
        }

        return param_parts.empty() ? std::wstring{STR("<none>")}
                                   : join_wide_parts(param_parts, STR(" | "));
    }

    auto read_function_param_value_as_string(RC::Unreal::UFunction* function, void* params,
                                             std::wstring_view param_name) -> std::wstring
    {
        if (!function || !params || param_name.empty())
        {
            return STR("");
        }

        RC::Unreal::FProperty* const property = find_function_param_by_name(function, param_name);
        if (!property)
        {
            return STR("");
        }

        const int offset = property->GetOffset_Internal();
        const int element_size = property->GetElementSize();
        if (offset < 0 || element_size <= 0)
        {
            return STR("");
        }

        const auto* const params_bytes = static_cast<const std::byte*>(params);
        const void* const value_ptr = params_bytes + offset;
        const std::wstring property_class = property->GetClass().GetName();
        if (property_class == STR("StrProperty"))
        {
            if (const RC::Unreal::FString* const string_value =
                    property->ContainerPtrToValuePtr<RC::Unreal::FString>(params))
            {
                if (const wchar_t* chars = string_value->GetCharArray())
                {
                    return std::wstring{chars};
                }
            }
            return STR("");
        }
        if (property_class == STR("NameProperty"))
        {
            if (const RC::Unreal::FName* const name_value =
                    property->ContainerPtrToValuePtr<RC::Unreal::FName>(params))
            {
                const std::wstring direct_name = name_value->ToString();
                if (!direct_name.empty() && direct_name != STR("None"))
                {
                    return direct_name;
                }
            }
        }
        return format_hook_param_value(property, value_ptr, element_size);
    }

    auto read_function_int_param(RC::Unreal::UFunction* function_object, void* params,
                                 std::wstring_view param_name) -> std::optional<int64_t>
    {
        if (!function_object || !params || param_name.empty())
        {
            return std::nullopt;
        }

        RC::Unreal::FProperty* const property =
            find_function_param_by_name(function_object, param_name);
        if (!property)
        {
            return std::nullopt;
        }

        const std::wstring property_class = property->GetClass().GetName();
        if (property_class != STR("IntProperty") && property_class != STR("UInt32Property") &&
            property_class != STR("ByteProperty") && property_class != STR("EnumProperty"))
        {
            return std::nullopt;
        }

        const int offset = property->GetOffset_Internal();
        const int element_size = property->GetElementSize();
        if (offset < 0 || element_size <= 0)
        {
            return std::nullopt;
        }

        const auto* const params_bytes = static_cast<const std::byte*>(params);
        return static_cast<int64_t>(read_unsigned_param_value(params_bytes + offset, element_size));
    }

    auto summarize_function_params_compact(RC::Unreal::UFunction* function_object, void* params)
        -> std::wstring
    {
        if (!function_object || !params)
        {
            return STR("<none>");
        }

        std::vector<std::wstring> parts{};
        if (function_object->GetName() == STR("OnCallback"))
        {
            if (const auto id = read_function_int_param(function_object, params, STR("ID"));
                id.has_value())
            {
                parts.push_back(std::format(STR("ID:IntProperty={}"), *id));
            }
            if (const auto param = read_function_int_param(function_object, params, STR("Param"));
                param.has_value())
            {
                parts.push_back(std::format(STR("Param:IntProperty={}"), *param));
            }
        }
        else if (function_object->GetName() == STR("OnListIndexChange"))
        {
            if (const auto list_index =
                    read_function_int_param(function_object, params, STR("ListIndex"));
                list_index.has_value())
            {
                parts.push_back(std::format(STR("ListIndex:IntProperty={}"), *list_index));
            }
            if (const auto prev_list_index =
                    read_function_int_param(function_object, params, STR("PrevListIndex"));
                prev_list_index.has_value())
            {
                parts.push_back(std::format(STR("PrevListIndex:IntProperty={}"), *prev_list_index));
            }
        }
        else if (function_object->GetName() == STR("OnAnchorIn") ||
                 function_object->GetName() == STR("OnAnchorOut"))
        {
            if (const auto index = read_function_int_param(function_object, params, STR("Index"));
                index.has_value())
            {
                parts.push_back(std::format(STR("Index:IntProperty={}"), *index));
            }
            if (const auto dir = read_function_int_param(function_object, params, STR("Dir"));
                dir.has_value())
            {
                parts.push_back(std::format(STR("Dir:IntProperty={}"), *dir));
            }
        }

        return parts.empty() ? std::wstring{STR("<none>")} : join_wide_parts(parts, STR(" | "));
    }

    auto function_param_signature(RC::Unreal::UFunction* function_object) -> std::wstring
    {
        if (!function_object)
        {
            return STR("");
        }

        std::wostringstream stream;
        bool first = true;
        for (RC::Unreal::FProperty* param = function_object->GetFirstProperty(); param;
             param = param->HasNext() ? param->GetNextFieldAsProperty() : nullptr)
        {
            if (!param || !param->HasAnyPropertyFlags(RC::Unreal::CPF_Parm))
            {
                continue;
            }
            if (!first)
            {
                stream << STR(", ");
            }
            first = false;
            stream << param->GetName() << STR(":") << param->GetClass().GetName();
            if (param->HasAnyPropertyFlags(RC::Unreal::CPF_ReturnParm))
            {
                stream << STR(":return");
            }
        }
        return stream.str();
    }

    auto format_object_debug_name(RC::Unreal::UObject* object) -> std::wstring
    {
        if (!object)
        {
            return std::wstring{STR("<null>")};
        }

        const auto object_address = reinterpret_cast<uintptr_t>(object);
        if (object_address < minimum_object_address || (object_address % alignof(void*)) != 0)
        {
            return std::format(STR("<invalid> @{} real=false"), object_address);
        }

        std::wstring full_name{};
        if (!kh3ap::try_get_uobject_full_name_fast(object, full_name))
        {
            return std::format(STR("<invalid> @{} real=false"), object_address);
        }

        return std::format(STR("{} @{} real={}"), full_name, object_address, STR("true"));
    }

    auto summarize_object_class_chain(RC::Unreal::UObject* object, size_t max_depth) -> std::wstring
    {
        if (!object || !kh3ap::is_uobject_valid_fast(object))
        {
            return STR("<null>");
        }

        std::vector<std::wstring> class_parts{};
        for (RC::Unreal::UStruct* object_class = object->GetClassPrivate();
             object_class && class_parts.size() < max_depth;
             object_class = object_class->GetSuperStruct())
        {
            class_parts.push_back(object_class->GetName());
        }
        return class_parts.empty() ? std::wstring{STR("<none>")}
                                   : join_wide_parts(class_parts, STR("<"));
    }

    auto format_object_outer_chain(RC::Unreal::UObject* object, int max_depth) -> std::wstring
    {
        if (!object)
        {
            return STR("<null>");
        }

        std::vector<std::wstring> parts{};
        RC::Unreal::UObject* current = object;
        for (int depth = 0; current && depth < max_depth; ++depth)
        {
            parts.emplace_back(format_object_debug_name(current));
            RC::Unreal::UObject* next_outer = nullptr;
            try
            {
                next_outer = current->GetOuterPrivate();
            }
            catch (...)
            {
                parts.emplace_back(STR("<GetOuterPrivate threw>"));
                break;
            }

            if (!next_outer || next_outer == current)
            {
                break;
            }
            current = next_outer;
        }

        std::wostringstream stream;
        for (size_t index = 0; index < parts.size(); ++index)
        {
            if (index > 0)
            {
                stream << STR(" -> ");
            }
            stream << parts[index];
        }
        return stream.str();
    }
} // namespace kh3ap
