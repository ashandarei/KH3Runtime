#include "kh3ap/diagnostics/ability_probe/equipment_layout.h"
#include "kh3ap/diagnostics/ability_probe/limits.h"
#include "kh3ap/diagnostics/ability_probe/ability_reflection_probe_helpers.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/bridge/utilities.h"
#include <Unreal/UObject.hpp>
#include <Unreal/UClass.hpp>
#include <Unreal/FProperty.hpp>
#include <algorithm>
#include <array>
#include <format>

namespace kh3ap
{
    using namespace ability_probe_limits;
    using namespace ability_probe_detail;
    auto prepare_deep_target_object_probe_entries(
            RC::Unreal::UObject* object,
            std::wstring_view source_property) -> std::vector<PropertyProbeEntry>
    {
        std::vector<PropertyProbeEntry> entries{};
        if (!object || !kh3ap::is_uobject_valid_fast(object))
        {
            return entries;
        }

        RC::Unreal::UClass* object_class = object->GetClassPrivate();
        if (!object_class)
        {
            return entries;
        }

        const std::wstring object_name = object->GetFullName();
        const std::wstring class_name = object_class->GetName();
        entries.push_back(PropertyProbeEntry{
                .kind = STR("deep_object"),
                .object_name = object_name,
                .class_name = class_name,
                .property_name = std::wstring{source_property},
                .property_class = STR("source"),
                .value = format_object_debug_name(object),
        });

        if (class_name == STR("TresFormAbilitySet"))
        {
            entries.push_back(PropertyProbeEntry{
                    .kind = STR("deep_property"),
                    .object_name = object_name,
                    .class_name = class_name,
                    .property_name = STR("m_Data"),
                    .property_class = STR("ManualMap"),
                    .offset = form_ability_data_offset,
                    .element_size = 0,
                    .value = truncate_for_log(format_manual_map_at_property(object, STR("m_Data"), broad_asset_map_entry_bytes), array_value_log_chars),
            });
        }
        else if (class_name == STR("TresEquipKeybladeData"))
        {
            entries.push_back(PropertyProbeEntry{
                    .kind = STR("deep_property"),
                    .object_name = object_name,
                    .class_name = class_name,
                    .property_name = STR("m_EquipMap"),
                    .property_class = STR("ManualMap"),
                    .offset = keyblade_equipment_map_offset,
                    .element_size = 0,
                    .value = truncate_for_log(format_manual_map_at_property(object, STR("m_EquipMap"), broad_asset_map_entry_bytes), array_value_log_chars),
            });
        }
        else if (class_name == STR("TresChrBaseParam"))
        {
            const auto* object_bytes = reinterpret_cast<const std::byte*>(object);
            for (int chunk = 0; chunk < deep_parameter_chunk_count; ++chunk)
            {
                const size_t offset = static_cast<size_t>(chunk) * parameter_chunk_bytes;
                entries.push_back(PropertyProbeEntry{
                        .kind = STR("deep_property"),
                        .object_name = object_name,
                        .class_name = class_name,
                        .property_name = std::format(STR("raw_0x{:03X}"), offset),
                        .property_class = STR("ManualRaw32"),
                        .offset = static_cast<int>(offset),
                        .element_size = parameter_chunk_bytes,
                        .value = format_bytes_hex(object_bytes + offset, parameter_chunk_bytes),
                });
            }
        }

        int logged_property_count = 0;
        for (RC::Unreal::FProperty* property : object_class->ForEachPropertyInChain())
        {
            if (!property)
            {
                continue;
            }

            const std::wstring property_name = property->GetName();
            const std::wstring property_class = property->GetClass().GetName();
            if (!should_probe_live_ability_owner_property(property_name, property_class) &&
                !contains_any_wide(property_name, {STR("Base"), STR("Param"), STR("Data"), STR("Map"), STR("Equip"), STR("Ability")}))
            {
                continue;
            }

            const int offset = property->GetOffset_Internal();
            const int element_size = property->GetElementSize();
            if (offset < 0 || element_size <= 0)
            {
                continue;
            }

            const auto* object_bytes = reinterpret_cast<const std::byte*>(object);
            const void* value_ptr = object_bytes + offset;
            const std::wstring value = property_class == STR("ArrayProperty")
                                               ? format_live_ability_owner_array_value(property, value_ptr)
                                               : format_ability_state_property_value(object, property, value_ptr, element_size);
            entries.push_back(PropertyProbeEntry{
                    .kind = STR("deep_reflected_property"),
                    .object_name = object_name,
                    .class_name = class_name,
                    .property_name = property_name,
                    .property_class = property_class,
                    .offset = offset,
                    .element_size = element_size,
                    .value = truncate_for_log(value, deep_value_log_chars),
            });

            ++logged_property_count;
            if (logged_property_count >= max_deep_target_properties)
            {
                break;
            }
        }

        return entries;
    }

    auto prepare_tres_equipment_component_offset_probe_data(
            RC::Unreal::UObject* object,
            bool include_deep_targets) -> EquipmentComponentOffsetProbeData
    {
        EquipmentComponentOffsetProbeData data{};
        if (!object || !kh3ap::is_uobject_valid_fast(object))
        {
            return data;
        }

        RC::Unreal::UClass* object_class = object->GetClassPrivate();
        if (!object_class || object_class->GetName() != STR("TresEquipmentComponent"))
        {
            return data;
        }

        const auto* object_bytes = reinterpret_cast<const std::byte*>(object);
        const std::wstring object_name = object->GetFullName();
        const std::wstring class_name = object_class->GetName();
        for (const EquipmentOffsetProbe& probe : equipment_offset_probes)
        {
            std::wstring value{};
            if (probe.kind == STR("ManualObject"))
            {
                value = format_manual_object_pointer_value(object_bytes + probe.offset);
            }
            else if (probe.kind == STR("ManualObjectArray") || probe.kind == STR("ManualStructArray"))
            {
                value = format_manual_array_at_offset(object_bytes, probe.offset, probe.assumed_element_size);
            }
            else
            {
                value = format_bytes_hex(object_bytes + probe.offset, static_cast<size_t>(probe.assumed_element_size));
            }

            data.entries.push_back(PropertyProbeEntry{
                    .kind = STR("offset_property"),
                    .object_name = object_name,
                    .class_name = class_name,
                    .property_name = std::wstring{probe.name},
                    .property_class = std::wstring{probe.kind},
                    .offset = static_cast<int>(probe.offset),
                    .element_size = probe.assumed_element_size,
                    .value = truncate_for_log(value, deep_value_log_chars),
            });

            if (include_deep_targets &&
                probe.kind == STR("ManualObject") &&
                (probe.name == STR("m_FormAbilityAsset") ||
                 probe.name == STR("m_EquipSlots.m_pAsset") ||
                 probe.name == STR("m_pBaseParam")))
            {
                data.deep_targets.push_back(EquipmentComponentDeepTarget{
                        .object = read_manual_object_pointer(object_bytes + probe.offset),
                        .source_property = std::wstring{probe.name},
                });
            }
        }

        return data;
    }

    auto prepare_targeted_equipment_reached_asset_map_entries(
            RC::Unreal::UObject* object,
            std::wstring_view source_property) -> std::vector<PropertyProbeEntry>
    {
        std::vector<PropertyProbeEntry> entries{};
        if (!object || !kh3ap::is_uobject_valid_fast(object))
        {
            return entries;
        }

        RC::Unreal::UClass* object_class = object->GetClassPrivate();
        if (!object_class)
        {
            return entries;
        }

        const std::wstring object_name = object->GetFullName();
        const std::wstring class_name = object_class->GetName();
        entries.push_back(PropertyProbeEntry{
                .kind = STR("equipment_asset_object"),
                .object_name = object_name,
                .class_name = class_name,
                .property_name = std::wstring{source_property},
                .property_class = STR("ManualObject"),
                .value = format_object_debug_name(object),
        });

        if (class_name == STR("TresFormAbilitySet"))
        {
            entries.push_back(PropertyProbeEntry{
                    .kind = STR("equipment_asset_map"),
                    .object_name = object_name,
                    .class_name = class_name,
                    .property_name = STR("m_Data"),
                    .property_class = STR("ManualMap"),
                    .offset = form_ability_data_offset,
                    .element_size = targeted_form_map_entry_bytes,
                    .value = truncate_for_log(format_manual_map_at_property(object, STR("m_Data"), targeted_form_map_entry_bytes), raw_map_log_chars),
            });
            entries.push_back(PropertyProbeEntry{
                    .kind = STR("equipment_asset_map_decoded"),
                    .object_name = object_name,
                    .class_name = class_name,
                    .property_name = STR("m_Data"),
                    .property_class = STR("ManualMapNestedArrays"),
                    .offset = form_ability_data_offset,
                    .element_size = targeted_form_map_entry_bytes,
                    .value = truncate_for_log(format_manual_map_nested_arrays_at_property(object, STR("m_Data")), nested_map_log_chars),
            });
        }
        else if (class_name == STR("TresEquipKeybladeData"))
        {
            entries.push_back(PropertyProbeEntry{
                    .kind = STR("equipment_asset_map"),
                    .object_name = object_name,
                    .class_name = class_name,
                    .property_name = STR("m_EquipMap"),
                    .property_class = STR("ManualMap"),
                    .offset = keyblade_equipment_map_offset,
                    .element_size = targeted_equipment_map_entry_bytes,
                    .value = truncate_for_log(format_manual_map_at_property(object, STR("m_EquipMap"), targeted_equipment_map_entry_bytes), raw_map_log_chars),
            });
            entries.push_back(PropertyProbeEntry{
                    .kind = STR("equipment_asset_map_decoded"),
                    .object_name = object_name,
                    .class_name = class_name,
                    .property_name = STR("m_EquipMap"),
                    .property_class = STR("ManualMapNestedArrays"),
                    .offset = keyblade_equipment_map_offset,
                    .element_size = targeted_equipment_map_entry_bytes,
                    .value = truncate_for_log(format_manual_map_nested_arrays_at_property(object, STR("m_EquipMap")), nested_map_log_chars),
            });
        }
        else if (class_name == STR("TresChrBaseParam"))
        {
            const auto* object_bytes = reinterpret_cast<const std::byte*>(object);
            for (int chunk = 0; chunk < targeted_parameter_chunk_count; ++chunk)
            {
                const size_t offset = static_cast<size_t>(chunk) * parameter_chunk_bytes;
                entries.push_back(PropertyProbeEntry{
                        .kind = STR("equipment_asset_raw"),
                        .object_name = object_name,
                        .class_name = class_name,
                        .property_name = std::format(STR("raw_0x{:03X}"), offset),
                        .property_class = STR("ManualRaw32"),
                        .offset = static_cast<int>(offset),
                        .element_size = parameter_chunk_bytes,
                        .value = format_bytes_hex(object_bytes + offset, parameter_chunk_bytes),
                });
            }
        }

        return entries;
    }

    auto prepare_targeted_equipment_manual_offset_probe_data(
            RC::Unreal::UObject* equipment_object) -> EquipmentComponentOffsetProbeData
    {
        EquipmentComponentOffsetProbeData data{};
        if (!equipment_object || !kh3ap::is_uobject_valid_fast(equipment_object))
        {
            return data;
        }

        RC::Unreal::UClass* equipment_class = equipment_object->GetClassPrivate();
        if (!equipment_class || equipment_class->GetName() != STR("TresEquipmentComponent"))
        {
            return data;
        }

        const auto* object_bytes = reinterpret_cast<const std::byte*>(equipment_object);
        const std::wstring object_name = equipment_object->GetFullName();
        const std::wstring class_name = equipment_class->GetName();
        for (const EquipmentOffsetProbe& probe : equipment_offset_probes)
        {
            std::wstring value{};
            if (probe.kind == STR("ManualObject"))
            {
                value = format_manual_object_pointer_value(object_bytes + probe.offset);
            }
            else if (probe.kind == STR("ManualObjectArray") || probe.kind == STR("ManualStructArray"))
            {
                value = format_manual_array_at_offset(object_bytes, probe.offset, probe.assumed_element_size);
            }
            else
            {
                value = format_bytes_hex(object_bytes + probe.offset, static_cast<size_t>(probe.assumed_element_size));
            }

            data.entries.push_back(PropertyProbeEntry{
                    .kind = STR("equipment_offset"),
                    .object_name = object_name,
                    .class_name = class_name,
                    .property_name = std::wstring{probe.name},
                    .property_class = std::wstring{probe.kind},
                    .offset = static_cast<int>(probe.offset),
                    .element_size = probe.assumed_element_size,
                    .value = truncate_for_log(value, targeted_value_log_chars),
            });

            if (probe.kind == STR("ManualObject") &&
                (probe.name == STR("m_FormAbilityAsset") ||
                 probe.name == STR("m_EquipSlots.m_pAsset") ||
                 probe.name == STR("m_pBaseParam")))
            {
                data.deep_targets.push_back(EquipmentComponentDeepTarget{
                        .object = read_manual_object_pointer(object_bytes + probe.offset),
                        .source_property = std::wstring{probe.name},
                });
            }
        }

        return data;
    }

    auto prepare_targeted_sora_equipment_link_probe_data(
            RC::Unreal::UObject* pawn_object) -> TargetedSoraEquipmentLinkProbeData
    {
        TargetedSoraEquipmentLinkProbeData data{};
        if (!pawn_object || !kh3ap::is_uobject_valid_fast(pawn_object))
        {
            return data;
        }

        RC::Unreal::UClass* pawn_class = pawn_object->GetClassPrivate();
        const std::wstring pawn_name = pawn_object->GetFullName();
        const std::wstring pawn_class_name = pawn_class ? pawn_class->GetName() : std::wstring{STR("<null>")};
        RC::Unreal::UObject* equipment_object = try_read_reflected_object_property_value(pawn_object, STR("MyEquipment"));
        if (equipment_object && kh3ap::is_uobject_valid_fast(equipment_object))
        {
            RC::Unreal::UClass* equipment_class = equipment_object->GetClassPrivate();
            data.entry = PropertyProbeEntry{
                    .kind = STR("sora_equipment_link"),
                    .object_name = pawn_name,
                    .class_name = pawn_class_name,
                    .property_name = STR("MyEquipment"),
                    .property_class = equipment_class ? equipment_class->GetName() : std::wstring{STR("<null>")},
                    .value = format_object_debug_name(equipment_object),
            };
            data.equipment_object = equipment_object;
            return data;
        }

        data.entry = PropertyProbeEntry{
                .kind = STR("sora_equipment_link_missing"),
                .object_name = pawn_name,
                .class_name = pawn_class_name,
                .property_name = STR("MyEquipment"),
                .property_class = STR("ObjectProperty"),
                .value = STR("<null>"),
        };
        return data;
    }
}
