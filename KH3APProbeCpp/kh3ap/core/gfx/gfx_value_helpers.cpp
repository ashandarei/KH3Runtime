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

namespace kh3ap
{
    using gfx_detail::find_as_value_input_property;
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

    auto summarize_as_value_struct(void* value_ptr, FProperty* value_property) -> std::wstring
    {
        if (!value_ptr || !value_property ||
            value_property->GetClass().GetName() != STR("StructProperty"))
        {
            return STR("<none>");
        }
        auto* const struct_property = static_cast<FStructProperty*>(value_property);
        UScriptStruct* const script_struct =
            struct_property ? struct_property->GetStruct() : nullptr;
        if (!script_struct)
        {
            return STR("<none>");
        }

        std::vector<std::wstring> parts{};
        uint64_t as_type = 0;
        if (FProperty* const as_type_field =
                find_struct_field_by_name(script_struct, STR("ASType")))
        {
            if (void* const as_type_ptr = as_type_field->ContainerPtrToValuePtr<void>(value_ptr))
            {
                as_type = read_unsigned_param_value(as_type_ptr, as_type_field->GetElementSize());
                parts.push_back(std::format(STR("ASType={}"), as_type));
            }
        }
        if (as_type == 0)
        {
            return parts.empty() ? std::wstring{STR("<empty>")} : join_wide_parts(parts, STR(", "));
        }
        for (FProperty* field = script_struct->GetFirstProperty(); field;
             field = field->HasNext() ? field->GetNextFieldAsProperty() : nullptr)
        {
            if (!field)
            {
                continue;
            }
            const std::wstring field_name = field->GetName();
            if (field_name == STR("ASType"))
            {
                continue;
            }
            if ((as_type == 2 && field_name != STR("ASBoolean")) ||
                (as_type == 3 && field_name != STR("ASInt")) ||
                (as_type == 4 && field_name != STR("ASNumber")) ||
                ((as_type == 5 || as_type == 6) && field_name != STR("ASString")) ||
                ((as_type == 7 || as_type == 8) && field_name != STR("ASObject")))
            {
                continue;
            }
            const std::wstring field_class = field->GetClass().GetName();
            void* const field_ptr = field->ContainerPtrToValuePtr<void>(value_ptr);
            if (!field_ptr)
            {
                continue;
            }

            std::wstring value{};
            if (field_class == STR("StrProperty"))
            {
                if (FString* const string_value = field->ContainerPtrToValuePtr<FString>(value_ptr))
                {
                    if (const wchar_t* chars = string_value->GetCharArray())
                    {
                        value = chars;
                    }
                }
            }
            else if (field_class == STR("ObjectProperty"))
            {
                UObject* const object_value = *static_cast<UObject**>(field_ptr);
                value = format_object_debug_name(object_value);
            }
            else if (field_class == STR("BoolProperty"))
            {
                value = (*static_cast<bool*>(field_ptr)) ? STR("true") : STR("false");
            }
            else if (field_class == STR("IntProperty") || field_class == STR("ByteProperty") ||
                     field_class == STR("EnumProperty"))
            {
                value =
                    std::to_wstring(read_unsigned_param_value(field_ptr, field->GetElementSize()));
            }
            else if (field_class == STR("FloatProperty"))
            {
                float number = 0.0f;
                std::memcpy(&number, field_ptr, sizeof(number));
                value = std::to_wstring(number);
            }

            if (!value.empty() && value != STR("<null>"))
            {
                parts.push_back(
                    std::format(STR("{}={}"), field_name, truncate_for_log(value, 160)));
            }
        }

        return parts.empty() ? std::wstring{STR("<empty>")} : join_wide_parts(parts, STR(", "));
    }

    auto try_get_as_value_object(void* value_ptr, FProperty* value_property) -> UObject*
    {
        if (!value_ptr || !value_property ||
            value_property->GetClass().GetName() != STR("StructProperty"))
        {
            return nullptr;
        }
        auto* const struct_property = static_cast<FStructProperty*>(value_property);
        UScriptStruct* const script_struct =
            struct_property ? struct_property->GetStruct() : nullptr;
        if (!script_struct)
        {
            return nullptr;
        }
        FProperty* const as_type_field = find_struct_field_by_name(script_struct, STR("ASType"));
        FProperty* const object_field = find_struct_field_by_name(script_struct, STR("ASObject"));
        if (!as_type_field || !object_field ||
            object_field->GetClass().GetName() != STR("ObjectProperty"))
        {
            return nullptr;
        }
        void* const as_type_ptr = as_type_field->ContainerPtrToValuePtr<void>(value_ptr);
        void* const object_ptr = object_field->ContainerPtrToValuePtr<void>(value_ptr);
        if (!as_type_ptr || !object_ptr)
        {
            return nullptr;
        }
        const uint64_t as_type =
            read_unsigned_param_value(as_type_ptr, as_type_field->GetElementSize());
        if (as_type != 7 && as_type != 8)
        {
            return nullptr;
        }
        return *static_cast<UObject**>(object_ptr);
    }

    auto try_get_as_value_number(void* value_ptr, FProperty* value_property)
        -> std::optional<double>
    {
        if (!value_ptr || !value_property ||
            value_property->GetClass().GetName() != STR("StructProperty"))
        {
            return std::nullopt;
        }
        auto* const struct_property = static_cast<FStructProperty*>(value_property);
        UScriptStruct* const script_struct =
            struct_property ? struct_property->GetStruct() : nullptr;
        if (!script_struct)
        {
            return std::nullopt;
        }
        FProperty* const as_type_field = find_struct_field_by_name(script_struct, STR("ASType"));
        void* const as_type_ptr =
            as_type_field ? as_type_field->ContainerPtrToValuePtr<void>(value_ptr) : nullptr;
        if (!as_type_ptr)
        {
            return std::nullopt;
        }
        const uint64_t as_type =
            read_unsigned_param_value(as_type_ptr, as_type_field->GetElementSize());
        if (as_type == 3)
        {
            FProperty* const field = find_struct_field_by_name(script_struct, STR("ASInt"));
            void* const field_ptr =
                field ? field->ContainerPtrToValuePtr<void>(value_ptr) : nullptr;
            if (!field_ptr)
            {
                return std::nullopt;
            }
            int32_t value{};
            std::memcpy(&value, field_ptr,
                        std::min(sizeof(value), static_cast<size_t>(field->GetElementSize())));
            return static_cast<double>(value);
        }
        if (as_type != 4)
        {
            return std::nullopt;
        }
        FProperty* const field = find_struct_field_by_name(script_struct, STR("ASNumber"));
        void* const field_ptr = field ? field->ContainerPtrToValuePtr<void>(value_ptr) : nullptr;
        if (!field_ptr)
        {
            return std::nullopt;
        }
        const std::wstring field_class = field->GetClass().GetName();
        if (field_class == STR("FloatProperty"))
        {
            float value{};
            std::memcpy(&value, field_ptr, sizeof(value));
            return static_cast<double>(value);
        }
        if (field_class == STR("DoubleProperty"))
        {
            double value{};
            std::memcpy(&value, field_ptr, sizeof(value));
            return value;
        }
        return std::nullopt;
    }

    auto try_get_as_value_string(void* value_ptr, FProperty* value_property)
        -> std::optional<std::wstring>
    {
        if (!value_ptr || !value_property ||
            value_property->GetClass().GetName() != STR("StructProperty"))
        {
            return std::nullopt;
        }
        auto* const struct_property = static_cast<FStructProperty*>(value_property);
        UScriptStruct* const script_struct =
            struct_property ? struct_property->GetStruct() : nullptr;
        if (!script_struct)
        {
            return std::nullopt;
        }
        FProperty* const as_type_field = find_struct_field_by_name(script_struct, STR("ASType"));
        FProperty* const as_string_field =
            find_struct_field_by_name(script_struct, STR("ASString"));
        void* const as_type_ptr =
            as_type_field ? as_type_field->ContainerPtrToValuePtr<void>(value_ptr) : nullptr;
        if (!as_type_ptr || !as_string_field ||
            as_string_field->GetClass().GetName() != STR("StrProperty"))
        {
            return std::nullopt;
        }
        const uint64_t as_type =
            read_unsigned_param_value(as_type_ptr, as_type_field->GetElementSize());
        if (as_type != 5 && as_type != 6)
        {
            return std::nullopt;
        }
        FString* const string_value = as_string_field->ContainerPtrToValuePtr<FString>(value_ptr);
        if (!string_value)
        {
            return std::nullopt;
        }
        const wchar_t* const chars = string_value->GetCharArray();
        return chars ? std::optional<std::wstring>{std::wstring{chars}}
                     : std::optional<std::wstring>{std::wstring{}};
    }

    auto write_as_value_int(FProperty* value_property, void* value_ptr, int32_t value) -> bool
    {
        if (!value_property || !value_ptr ||
            value_property->GetClass().GetName() != STR("StructProperty"))
        {
            return false;
        }
        auto* const struct_property = static_cast<FStructProperty*>(value_property);
        UScriptStruct* const script_struct =
            struct_property ? struct_property->GetStruct() : nullptr;
        if (!script_struct)
        {
            return false;
        }
        std::memset(value_ptr, 0,
                    static_cast<size_t>(std::max(value_property->GetElementSize(), 0)));
        FProperty* const as_type_field = find_struct_field_by_name(script_struct, STR("ASType"));
        FProperty* const as_int_field = find_struct_field_by_name(script_struct, STR("ASInt"));
        if (!as_type_field || !as_int_field)
        {
            return false;
        }
        if (void* const as_type_ptr = as_type_field->ContainerPtrToValuePtr<void>(value_ptr))
        {
            const uint8_t type_int = 3;
            std::memcpy(
                as_type_ptr, &type_int,
                std::min(sizeof(type_int), static_cast<size_t>(as_type_field->GetElementSize())));
        }
        if (void* const as_int_ptr = as_int_field->ContainerPtrToValuePtr<void>(value_ptr))
        {
            std::memcpy(
                as_int_ptr, &value,
                std::min(sizeof(value), static_cast<size_t>(as_int_field->GetElementSize())));
            return true;
        }
        return false;
    }

    auto write_as_value_bool(FProperty* value_property, void* value_ptr, bool value) -> bool
    {
        if (!value_property || !value_ptr ||
            value_property->GetClass().GetName() != STR("StructProperty"))
        {
            return false;
        }
        auto* const struct_property = static_cast<FStructProperty*>(value_property);
        UScriptStruct* const script_struct =
            struct_property ? struct_property->GetStruct() : nullptr;
        if (!script_struct)
        {
            return false;
        }
        std::memset(value_ptr, 0,
                    static_cast<size_t>(std::max(value_property->GetElementSize(), 0)));
        FProperty* const as_type_field = find_struct_field_by_name(script_struct, STR("ASType"));
        FProperty* const as_bool_field = find_struct_field_by_name(script_struct, STR("ASBoolean"));
        if (!as_type_field || !as_bool_field ||
            as_bool_field->GetClass().GetName() != STR("BoolProperty"))
        {
            return false;
        }
        if (void* const as_type_ptr = as_type_field->ContainerPtrToValuePtr<void>(value_ptr))
        {
            const uint8_t type_bool = 2;
            std::memcpy(
                as_type_ptr, &type_bool,
                std::min(sizeof(type_bool), static_cast<size_t>(as_type_field->GetElementSize())));
        }
        auto* const bool_property = static_cast<RC::Unreal::FBoolProperty*>(as_bool_field);
        bool_property->SetPropertyValueInContainer(value_ptr, value);
        return bool_property->GetPropertyValueInContainer(value_ptr) == value;
    }

    auto write_as_value_number(FProperty* value_property, void* value_ptr, double value) -> bool
    {
        if (!value_property || !value_ptr ||
            value_property->GetClass().GetName() != STR("StructProperty"))
        {
            return false;
        }
        auto* const struct_property = static_cast<FStructProperty*>(value_property);
        UScriptStruct* const script_struct =
            struct_property ? struct_property->GetStruct() : nullptr;
        if (!script_struct)
        {
            return false;
        }
        std::memset(value_ptr, 0,
                    static_cast<size_t>(std::max(value_property->GetElementSize(), 0)));
        FProperty* const as_type_field = find_struct_field_by_name(script_struct, STR("ASType"));
        FProperty* const as_number_field =
            find_struct_field_by_name(script_struct, STR("ASNumber"));
        if (!as_type_field || !as_number_field)
        {
            return false;
        }
        if (void* const as_type_ptr = as_type_field->ContainerPtrToValuePtr<void>(value_ptr))
        {
            const uint8_t type_number = 4;
            std::memcpy(as_type_ptr, &type_number,
                        std::min(sizeof(type_number),
                                 static_cast<size_t>(as_type_field->GetElementSize())));
        }
        void* const number_ptr = as_number_field->ContainerPtrToValuePtr<void>(value_ptr);
        if (!number_ptr)
        {
            return false;
        }
        const std::wstring field_class = as_number_field->GetClass().GetName();
        if (field_class == STR("FloatProperty"))
        {
            const float float_value = static_cast<float>(value);
            std::memcpy(number_ptr, &float_value, sizeof(float_value));
            return true;
        }
        if (field_class == STR("DoubleProperty"))
        {
            std::memcpy(number_ptr, &value, sizeof(value));
            return true;
        }
        return false;
    }

    auto write_as_value_object(FProperty* value_property, void* value_ptr, UObject* value) -> bool
    {
        if (!value_property || !value_ptr || !value || !kh3ap::is_uobject_valid_fast(value) ||
            value_property->GetClass().GetName() != STR("StructProperty"))
        {
            return false;
        }
        auto* const struct_property = static_cast<FStructProperty*>(value_property);
        UScriptStruct* const script_struct =
            struct_property ? struct_property->GetStruct() : nullptr;
        if (!script_struct)
        {
            return false;
        }
        std::memset(value_ptr, 0,
                    static_cast<size_t>(std::max(value_property->GetElementSize(), 0)));
        FProperty* const as_type_field = find_struct_field_by_name(script_struct, STR("ASType"));
        FProperty* const as_object_field =
            find_struct_field_by_name(script_struct, STR("ASObject"));
        if (!as_type_field || !as_object_field ||
            as_object_field->GetClass().GetName() != STR("ObjectProperty"))
        {
            return false;
        }
        if (void* const as_type_ptr = as_type_field->ContainerPtrToValuePtr<void>(value_ptr))
        {
            const uint8_t type_object = 7;
            std::memcpy(as_type_ptr, &type_object,
                        std::min(sizeof(type_object),
                                 static_cast<size_t>(as_type_field->GetElementSize())));
        }
        UObject** const object_ptr = as_object_field->ContainerPtrToValuePtr<UObject*>(value_ptr);
        if (!object_ptr)
        {
            return false;
        }
        *object_ptr = value;
        return true;
    }

    auto write_as_value_string(FProperty* value_property, void* value_ptr, std::wstring_view value)
        -> bool
    {
        // Empty strings are valid AS3 arguments (for example an absent icon
        // path). They must retain String type rather than becoming undefined.
        if (!value_property || !value_ptr ||
            value_property->GetClass().GetName() != STR("StructProperty"))
        {
            return false;
        }
        auto* const struct_property = static_cast<FStructProperty*>(value_property);
        UScriptStruct* const script_struct =
            struct_property ? struct_property->GetStruct() : nullptr;
        if (!script_struct)
        {
            return false;
        }
        std::memset(value_ptr, 0,
                    static_cast<size_t>(std::max(value_property->GetElementSize(), 0)));
        FProperty* const as_type_field = find_struct_field_by_name(script_struct, STR("ASType"));
        FProperty* const as_string_field =
            find_struct_field_by_name(script_struct, STR("ASString"));
        if (!as_type_field || !as_string_field ||
            as_string_field->GetClass().GetName() != STR("StrProperty"))
        {
            return false;
        }
        if (void* const as_type_ptr = as_type_field->ContainerPtrToValuePtr<void>(value_ptr))
        {
            // UE4's Scaleform bridge distinguishes narrow AS_String (5)
            // from AS_StringW (6). FString is UTF-16 on Windows, so using 5
            // produces GFx's Japanese placeholder text for injected labels.
            const uint8_t type_string = 6;
            std::memcpy(as_type_ptr, &type_string,
                        std::min(sizeof(type_string),
                                 static_cast<size_t>(as_type_field->GetElementSize())));
        }
        if (FString* const as_string_ptr =
                as_string_field->ContainerPtrToValuePtr<FString>(value_ptr))
        {
            const std::wstring owned_value{value};
            *as_string_ptr = FString(owned_value.c_str());
            return true;
        }
        return false;
    }
    namespace gfx_detail
    {
        auto find_as_value_input_property(UFunction* function_object) -> FProperty*
        {
            if (!function_object)
            {
                return nullptr;
            }
            FProperty* value_property = find_function_param_by_name(function_object, STR("Input"));
            if (!value_property)
            {
                value_property = find_function_param_by_name(function_object, STR("Value"));
            }
            if (value_property && value_property->GetClass().GetName() == STR("StructProperty"))
            {
                return value_property;
            }
            for (FProperty* param = function_object->GetFirstProperty(); param;
                 param = param->HasNext() ? param->GetNextFieldAsProperty() : nullptr)
            {
                if (param && param->HasAnyPropertyFlags(CPF_Parm) &&
                    !param->HasAnyPropertyFlags(CPF_OutParm) &&
                    !param->HasAnyPropertyFlags(CPF_ReturnParm) &&
                    param->GetClass().GetName() == STR("StructProperty"))
                {
                    return param;
                }
            }
            return nullptr;
        }
    } // namespace gfx_detail

} // namespace kh3ap
