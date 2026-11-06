#pragma once

#include "kh3ap/diagnostics/ability_probe/ability_reflection_models.h"

#include <cstdint>
#include <cstddef>
#include <chrono>
#include <initializer_list>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace RC::Unreal
{
    struct FFrame;
    struct FName;
    class FProperty;
    class FStructProperty;
    class UClass;
    class UObject;
    class UFunction;
    class UStruct;
    class UScriptStruct;
}

namespace kh3ap
{
    auto collect_targeted_chrinit_candidates() -> std::vector<RC::Unreal::UObject*>;

    auto format_form_ability_enum_array(const ArrayHeader& header, int element_size) -> std::wstring;

    auto format_compact_form_ability_enum_array(const ArrayHeader& header, int element_size) -> std::wstring;

    auto form_ability_array_contains_value(const ArrayHeader& header, int element_size, uint64_t target_value) -> bool;

    auto format_chrinit_name_value(const void* value_ptr, int element_size) -> std::wstring;

    auto log_chrinit_array_value(
            std::wstring_view object_name,
            std::wstring_view struct_name,
            std::wstring_view field_name,
            RC::Unreal::FProperty* property,
            const void* value_ptr) -> void;

    auto format_chrinit_field_value(RC::Unreal::FProperty* property, const void* value_ptr) -> std::wstring;

    auto log_chrinit_struct_value(
            bool thunder_patch_enabled,
            RC::Unreal::UObject* object,
            std::wstring_view owner_property_name,
            RC::Unreal::FStructProperty* struct_property,
            void* struct_value_ptr) -> void;

    auto log_targeted_chrinit_probe(
            bool enabled,
            bool thunder_patch_enabled,
            std::wstring_view reason,
            std::wstring_view runtime_context) -> void;

    auto try_append_form_ability_byte(ArrayHeader& header, int element_size, uint8_t target_value) -> std::wstring_view;

    auto maybe_patch_chrinit_player_sora_thunder(
            bool enabled,
            std::wstring_view owner_property_name,
            std::wstring_view field_name,
            RC::Unreal::FProperty* property,
            void* value_ptr) -> std::wstring_view;
}
