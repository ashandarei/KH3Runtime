#include "kh3ap/diagnostics/ability_probe/limits.h"
#include "kh3ap/diagnostics/ability_probe/ability_reflection_probe_helpers.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/bridge/utilities.h"
#include <Unreal/UObject.hpp>
#include <Unreal/UClass.hpp>
#include <Unreal/FProperty.hpp>
#include <Unreal/Property/FArrayProperty.hpp>
#include <Unreal/Property/FStructProperty.hpp>
#include <Unreal/UScriptStruct.hpp>
#include <algorithm>
#include <array>
#include <cstring>
#include <format>

namespace kh3ap
{
    using namespace ability_probe_limits;
    auto prepare_ability_scalar_needle_match_entry(
            RC::Unreal::UObject* object,
            RC::Unreal::UClass* object_class,
            std::wstring_view property_path,
            RC::Unreal::FProperty* property,
            const void* value_ptr,
            int absolute_offset,
            int element_size) -> std::optional<AbilityArrayNeedleProbeEntry>
    {
        const auto formatted = try_format_ability_scalar_needle_value(property, value_ptr, element_size);
        if (!formatted.has_value())
        {
            return std::nullopt;
        }

        return AbilityArrayNeedleProbeEntry{
                .kind = STR("scalar_match"),
                .object_name = truncate_for_log(object ? object->GetFullName() : std::wstring{STR("<null>")}, object_name_log_chars),
                .class_name = object_class ? object_class->GetName() : std::wstring{STR("<null>")},
                .property_name = std::wstring{property_path},
                .property_class = property ? property->GetClass().GetName() : std::wstring{STR("<null>")},
                .inner_class = STR("scalar"),
                .offset = absolute_offset,
                .element_size = element_size,
                .inner_size = element_size,
                .array_num = -1,
                .array_max = -1,
                .matches = formatted->first,
                .value = truncate_for_log(formatted->second, scalar_value_log_chars),
        };
    }

    auto prepare_ability_map_needle_match_entry(
            RC::Unreal::UObject* object,
            RC::Unreal::UClass* object_class,
            std::wstring_view property_path,
            RC::Unreal::FProperty* property,
            const void* value_ptr,
            int absolute_offset,
            int element_size) -> std::optional<AbilityArrayNeedleProbeEntry>
    {
        const auto formatted = try_format_ability_map_needle_value(property, value_ptr);
        if (!formatted.has_value())
        {
            return std::nullopt;
        }

        MapHeader header{};
        std::memcpy(&header, value_ptr, sizeof(MapHeader));
        return AbilityArrayNeedleProbeEntry{
                .kind = STR("map_match"),
                .object_name = truncate_for_log(object ? object->GetFullName() : std::wstring{STR("<null>")}, object_name_log_chars),
                .class_name = object_class ? object_class->GetName() : std::wstring{STR("<null>")},
                .property_name = std::wstring{property_path},
                .property_class = property ? property->GetClass().GetName() : std::wstring{STR("<null>")},
                .inner_class = STR("map"),
                .offset = absolute_offset,
                .element_size = element_size,
                .inner_size = -1,
                .array_num = header.num,
                .array_max = header.max,
                .matches = formatted->first,
                .value = truncate_for_log(formatted->second, targeted_value_log_chars),
        };
    }

    auto prepare_ability_array_needle_match_entry(
            RC::Unreal::UObject* object,
            RC::Unreal::UClass* object_class,
            std::wstring_view property_path,
            RC::Unreal::FProperty* property,
            const void* value_ptr,
            int absolute_offset,
            int element_size) -> std::optional<AbilityArrayNeedleProbeEntry>
    {
        if (!object || !object_class || !property || !value_ptr)
        {
            return std::nullopt;
        }

        const auto formatted = try_format_ability_array_needle_value(property, value_ptr);
        if (!formatted.has_value())
        {
            return std::nullopt;
        }

        const auto* array_property = static_cast<RC::Unreal::FArrayProperty*>(property);
        RC::Unreal::FProperty* inner_property = const_cast<RC::Unreal::FProperty*>(array_property->GetInner());
        const std::wstring inner_class = inner_property ? inner_property->GetClass().GetName() : std::wstring{STR("<none>")};
        const int inner_size = inner_property ? inner_property->GetElementSize() : 0;
        ArrayHeader header{};
        std::memcpy(&header, value_ptr, sizeof(ArrayHeader));

        return AbilityArrayNeedleProbeEntry{
                .kind = STR("array_match"),
                .object_name = object->GetFullName(),
                .class_name = object_class->GetName(),
                .property_name = std::wstring{property_path},
                .property_class = property->GetClass().GetName(),
                .inner_class = inner_class,
                .offset = absolute_offset,
                .element_size = element_size,
                .inner_size = inner_size,
                .array_num = header.num,
                .array_max = header.max,
                .matches = formatted->first,
                .value = truncate_for_log(formatted->second, array_value_log_chars),
        };
    }

    auto prepare_manual_chrinit_sora_ability_array_entries(
            RC::Unreal::UObject* object,
            RC::Unreal::UClass* object_class) -> std::vector<AbilityArrayNeedleProbeEntry>
    {
        std::vector<AbilityArrayNeedleProbeEntry> entries{};
        if (!object || !object_class || object_class->GetName() != STR("TresChrInitData"))
        {
            return entries;
        }

        const auto* object_bytes = reinterpret_cast<const std::byte*>(object);
        struct ManualChrInitAbilityArray
        {
            std::wstring_view name;
            size_t offset;
        };

        constexpr size_t player_sora_offset = 0x48;
        const std::array<ManualChrInitAbilityArray, 4> arrays{{
                {STR("m_PlayerSora.m_EquipAbility"), player_sora_offset + 0x58},
                {STR("m_PlayerSora.m_HaveAbility"), player_sora_offset + 0x68},
                {STR("m_PlayerSora.m_CriticalEquipAbility"), player_sora_offset + 0x78},
                {STR("m_PlayerSora.m_CriticalHaveAbility"), player_sora_offset + 0x88},
        }};

        for (const ManualChrInitAbilityArray& array : arrays)
        {
            ArrayHeader header{};
            std::memcpy(&header, object_bytes + array.offset, sizeof(ArrayHeader));
            const auto formatted = try_format_manual_byte_ability_array_needle_value(header);
            if (!formatted.has_value())
            {
                continue;
            }

            entries.push_back(AbilityArrayNeedleProbeEntry{
                    .kind = STR("manual_chrinit_array_match"),
                    .object_name = STR("TresChrInitData"),
                    .class_name = object_class->GetName(),
                    .property_name = std::wstring{array.name},
                    .property_class = STR("ManualArray"),
                    .inner_class = STR("byte"),
                    .offset = static_cast<int>(array.offset),
                    .element_size = static_cast<int>(sizeof(ArrayHeader)),
                    .inner_size = 1,
                    .array_num = header.num,
                    .array_max = header.max,
                    .matches = formatted->first,
                    .value = truncate_for_log(formatted->second, deep_value_log_chars),
            });
        }

        return entries;
    }

    auto prepare_ability_array_needle_struct_match_entries(
            RC::Unreal::UObject* object,
            RC::Unreal::UClass* object_class,
            std::wstring_view outer_property_name,
            RC::Unreal::FStructProperty* struct_property,
            const std::byte* struct_base,
            int outer_offset,
            int max_entries) -> std::vector<AbilityArrayNeedleProbeEntry>
    {
        std::vector<AbilityArrayNeedleProbeEntry> entries{};
        if (!object || !object_class || !struct_property || !struct_base || max_entries <= 0)
        {
            return entries;
        }

        RC::Unreal::UScriptStruct* script_struct = struct_property->GetStruct();
        if (!script_struct)
        {
            return entries;
        }

        int nested_count = 0;
        for (RC::Unreal::FProperty* nested_property : script_struct->ForEachPropertyInChain())
        {
            if (!nested_property)
            {
                continue;
            }

            const int nested_offset = nested_property->GetOffset_Internal();
            const int nested_element_size = nested_property->GetElementSize();
            if (nested_offset < 0 || nested_element_size <= 0)
            {
                continue;
            }

            std::optional<AbilityArrayNeedleProbeEntry> entry{};
            const std::wstring nested_class = nested_property->GetClass().GetName();
            if (nested_class == STR("ArrayProperty"))
            {
                const void* nested_value_ptr = struct_base + nested_offset;
                const std::wstring property_path = std::format(STR("{}.{}"), outer_property_name, nested_property->GetName());
                entry = prepare_ability_array_needle_match_entry(
                        object,
                        object_class,
                        property_path,
                        nested_property,
                        nested_value_ptr,
                        outer_offset + nested_offset,
                        nested_element_size);
            }
            else if (should_probe_ability_scalar_or_map_property(nested_property->GetName()))
            {
                const void* nested_value_ptr = struct_base + nested_offset;
                const std::wstring property_path = std::format(STR("{}.{}"), outer_property_name, nested_property->GetName());
                if (nested_class == STR("MapProperty"))
                {
                    entry = prepare_ability_map_needle_match_entry(
                            object,
                            object_class,
                            property_path,
                            nested_property,
                            nested_value_ptr,
                            outer_offset + nested_offset,
                            nested_element_size);
                }
                else
                {
                    entry = prepare_ability_scalar_needle_match_entry(
                            object,
                            object_class,
                            property_path,
                            nested_property,
                            nested_value_ptr,
                            outer_offset + nested_offset,
                            nested_element_size);
                }
            }

            if (entry.has_value())
            {
                entries.push_back(std::move(*entry));
                if (static_cast<int>(entries.size()) >= max_entries)
                {
                    return entries;
                }
            }

            ++nested_count;
            if (nested_count >= max_nested_needle_properties)
            {
                break;
            }
        }

        return entries;
    }

    auto prepare_ability_array_needle_object_match_entries(
            RC::Unreal::UObject* object,
            RC::Unreal::UClass* object_class,
            int max_entries) -> std::vector<AbilityArrayNeedleProbeEntry>
    {
        std::vector<AbilityArrayNeedleProbeEntry> entries{};
        if (!object || !object_class || max_entries <= 0)
        {
            return entries;
        }

        auto append_entry = [&entries, max_entries](std::optional<AbilityArrayNeedleProbeEntry>&& entry) -> bool {
            if (entry.has_value())
            {
                entries.push_back(std::move(*entry));
            }
            return static_cast<int>(entries.size()) >= max_entries;
        };

        for (auto&& entry : prepare_manual_chrinit_sora_ability_array_entries(object, object_class))
        {
            entries.push_back(std::move(entry));
            if (static_cast<int>(entries.size()) >= max_entries)
            {
                return entries;
            }
        }

        for (RC::Unreal::FProperty* property : object_class->ForEachPropertyInChain())
        {
            if (!property)
            {
                continue;
            }

            const std::wstring property_class = property->GetClass().GetName();
            const int offset = property->GetOffset_Internal();
            const int element_size = property->GetElementSize();
            if (offset < 0 || element_size <= 0)
            {
                continue;
            }

            const auto* object_bytes = reinterpret_cast<const std::byte*>(object);
            const void* value_ptr = object_bytes + offset;

            if (property_class == STR("ArrayProperty"))
            {
                if (append_entry(prepare_ability_array_needle_match_entry(
                            object,
                            object_class,
                            property->GetName(),
                            property,
                            value_ptr,
                            offset,
                            element_size)))
                {
                    return entries;
                }
            }
            else if (property_class == STR("StructProperty"))
            {
                const int remaining = max_entries - static_cast<int>(entries.size());
                for (auto&& entry : prepare_ability_array_needle_struct_match_entries(
                             object,
                             object_class,
                             property->GetName(),
                             static_cast<RC::Unreal::FStructProperty*>(property),
                             static_cast<const std::byte*>(value_ptr),
                             offset,
                             remaining))
                {
                    entries.push_back(std::move(entry));
                    if (static_cast<int>(entries.size()) >= max_entries)
                    {
                        return entries;
                    }
                }
            }
            else if (should_probe_ability_scalar_or_map_property(property->GetName()))
            {
                if (property_class == STR("MapProperty"))
                {
                    if (append_entry(prepare_ability_map_needle_match_entry(
                                object,
                                object_class,
                                property->GetName(),
                                property,
                                value_ptr,
                                offset,
                                element_size)))
                    {
                        return entries;
                    }
                }
                else
                {
                    if (append_entry(prepare_ability_scalar_needle_match_entry(
                                object,
                                object_class,
                                property->GetName(),
                                property,
                                value_ptr,
                                offset,
                                element_size)))
                    {
                        return entries;
                    }
                }
            }
        }

        return entries;
    }

    auto prepare_ability_array_needle_probe_entries(
            RC::Unreal::UObject* preferred_object,
            RC::Unreal::UObject* last_world_context_object) -> std::vector<AbilityArrayNeedleProbeEntry>
    {
        std::vector<AbilityArrayNeedleProbeEntry> entries{};
        const auto candidates = collect_ability_array_needle_candidates(preferred_object, last_world_context_object);
        entries.push_back(AbilityArrayNeedleProbeEntry{
                .kind = STR("summary"),
                .object_name = format_object_debug_name(preferred_object),
                .class_name = std::format(STR("candidates={}"), static_cast<uint64_t>(candidates.size())),
        });

        int object_count = 0;
        int match_count = 0;
        for (RC::Unreal::UObject* object : candidates)
        {
            if (!is_ability_array_needle_candidate(object))
            {
                continue;
            }

            RC::Unreal::UClass* object_class = object->GetClassPrivate();
            if (!object_class)
            {
                continue;
            }

            entries.push_back(AbilityArrayNeedleProbeEntry{
                    .kind = STR("candidate"),
                    .object_name = truncate_for_log(object->GetFullName(), candidate_name_log_chars),
                    .class_name = object_class->GetName(),
            });

            int object_match_count = 0;
            const int max_entries = std::min(max_needle_matches_per_object - object_match_count, max_needle_matches - match_count);
            for (auto&& entry : prepare_ability_array_needle_object_match_entries(object, object_class, max_entries))
            {
                entries.push_back(std::move(entry));
                ++match_count;
                ++object_match_count;
                if (object_match_count >= max_needle_matches_per_object || match_count >= max_needle_matches)
                {
                    break;
                }
            }

            ++object_count;
            if (object_count >= max_needle_objects || match_count >= max_needle_matches)
            {
                break;
            }
        }

        return entries;
    }
}
