#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/features/grants/messages/ap_message_param_storage.h"

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
#include <array>
#include <cstring>
#include <string>

namespace kh3ap
{
    auto write_object_function_param(RC::Unreal::UFunction* function_object, void* params,
                                     std::wstring_view param_name, RC::Unreal::UObject* value)
        -> bool
    {
        if (!function_object || !params)
        {
            return false;
        }

        RC::Unreal::FProperty* const property =
            find_function_param_by_name(function_object, param_name);
        if (!property || property->GetClass().GetName() != STR("ObjectProperty"))
        {
            return false;
        }

        if (RC::Unreal::UObject** const value_ptr =
                property->ContainerPtrToValuePtr<RC::Unreal::UObject*>(params))
        {
            *value_ptr = value;
            return true;
        }
        return false;
    }

    auto write_bool_function_param(RC::Unreal::FProperty* property, void* params, bool value)
        -> bool
    {
        if (!property || !params || property->GetClass().GetName() != STR("BoolProperty"))
        {
            return false;
        }
        auto* const bool_property = static_cast<RC::Unreal::FBoolProperty*>(property);
        bool_property->SetPropertyValueInContainer(params, value);
        return bool_property->GetPropertyValueInContainer(params) == value;
    }

    auto write_linear_color_function_param(RC::Unreal::FProperty* property, void* params, float r,
                                           float g, float b, float a) -> bool
    {
        if (!property || !params || property->GetClass().GetName() != STR("StructProperty"))
        {
            return false;
        }

        const std::array color{r, g, b, a};
        void* const value_ptr = property->ContainerPtrToValuePtr<void>(params);
        if (!value_ptr || property->GetElementSize() < static_cast<int>(sizeof(color)))
        {
            return false;
        }

        std::memcpy(value_ptr, color.data(), sizeof(color));
        return true;
    }

    auto write_name_or_string_function_param(RC::Unreal::FProperty* property, void* params,
                                             std::wstring_view value) -> bool
    {
        if (!property || !params || value.empty())
        {
            return false;
        }

        const std::wstring property_class = property->GetClass().GetName();
        if (property_class == STR("NameProperty"))
        {
            if (RC::Unreal::FName* const name_value =
                    property->ContainerPtrToValuePtr<RC::Unreal::FName>(params))
            {
                *name_value = RC::Unreal::FName(value, RC::Unreal::FNAME_Add);
                return true;
            }
        }
        else if (property_class == STR("StrProperty"))
        {
            if (RC::Unreal::FString* const string_value =
                    property->ContainerPtrToValuePtr<RC::Unreal::FString>(params))
            {
                const std::wstring owned_value{value};
                *string_value = RC::Unreal::FString(owned_value.c_str());
                return true;
            }
        }
        else if (property_class == STR("TextProperty"))
        {
            if (RC::Unreal::FText* const text_value =
                    property->ContainerPtrToValuePtr<RC::Unreal::FText>(params))
            {
                const std::wstring owned_value{value};
                *text_value = RC::Unreal::FText(owned_value.c_str());
                return true;
            }
        }

        return false;
    }

    auto find_struct_field_by_name(RC::Unreal::UScriptStruct* script_struct,
                                   std::wstring_view field_name) -> RC::Unreal::FProperty*
    {
        if (!script_struct || field_name.empty())
        {
            return nullptr;
        }

        for (RC::Unreal::FProperty* field = script_struct->GetFirstProperty(); field;
             field = field->HasNext() ? field->GetNextFieldAsProperty() : nullptr)
        {
            if (field && field->GetName() == field_name)
            {
                return field;
            }
        }
        return nullptr;
    }

    auto write_string_struct_field(RC::Unreal::FProperty* field, void* struct_value,
                                   std::wstring_view value) -> bool
    {
        if (!field || !struct_value || field->GetClass().GetName() != STR("StrProperty"))
        {
            return false;
        }

        if (RC::Unreal::FString* const string_value =
                field->ContainerPtrToValuePtr<RC::Unreal::FString>(struct_value))
        {
            const std::wstring owned_value{value};
            *string_value = RC::Unreal::FString(owned_value.c_str());
            return true;
        }
        return false;
    }

    auto write_string_array_struct_field(RC::Unreal::FProperty* field, void* struct_value,
                                         std::wstring_view value,
                                         RuntimeApMessageParamStorage& storage) -> bool
    {
        if (!field || !struct_value || value.empty() ||
            field->GetClass().GetName() != STR("ArrayProperty"))
        {
            return false;
        }

        auto* const array_property = static_cast<RC::Unreal::FArrayProperty*>(field);
        RC::Unreal::FProperty* const inner_property =
            array_property ? const_cast<RC::Unreal::FProperty*>(array_property->GetInner())
                           : nullptr;
        if (!inner_property || inner_property->GetClass().GetName() != STR("StrProperty"))
        {
            return false;
        }

        ArrayHeader* const header = field->ContainerPtrToValuePtr<ArrayHeader>(struct_value);
        if (!header)
        {
            return false;
        }

        const auto retained =
            storage.retain_string_array(value, RuntimeApMessageParamStorage::max_entries);
        header->data_ptr = retained.data_ptr;
        header->num = retained.num;
        header->max = retained.max;
        return true;
    }

    auto write_tres_loc_text_function_param(RC::Unreal::FProperty* property, void* params,
                                            std::wstring_view localized_text,
                                            RuntimeApMessageParamStorage& storage) -> bool
    {
        if (!property || !params || localized_text.empty() ||
            property->GetClass().GetName() != STR("StructProperty"))
        {
            return false;
        }

        auto* const struct_property = static_cast<RC::Unreal::FStructProperty*>(property);
        RC::Unreal::UScriptStruct* const script_struct =
            struct_property ? struct_property->GetStruct() : nullptr;
        if (!script_struct || script_struct->GetName() != STR("TresLocText"))
        {
            return false;
        }

        void* const struct_value = property->ContainerPtrToValuePtr<void>(params);
        if (!struct_value)
        {
            return false;
        }

        // TresUI resolves this struct through locres. Use an existing vanilla
        // "{0}" entry and pass the runtime AP message as the first parameter.
        write_string_struct_field(find_struct_field_by_name(script_struct, STR("Namespace")),
                                  struct_value, STR("kh3_mobile.record"));
        write_string_struct_field(find_struct_field_by_name(script_struct, STR("Key")),
                                  struct_value, STR("record_0122"));
        write_string_struct_field(find_struct_field_by_name(script_struct, STR("LocalizedText")),
                                  struct_value, STR(""));
        if (!write_string_array_struct_field(
                find_struct_field_by_name(script_struct, STR("Params")), struct_value,
                localized_text, storage))
        {
            return false;
        }
        return true;
    }
} // namespace kh3ap
