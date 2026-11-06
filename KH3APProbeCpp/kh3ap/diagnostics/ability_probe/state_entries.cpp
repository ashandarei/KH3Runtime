#include "kh3ap/diagnostics/ability_probe/limits.h"
#include "kh3ap/diagnostics/ability_probe/ability_reflection_probe_helpers.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/bridge/utilities.h"
#include <Unreal/UObject.hpp>
#include <Unreal/UClass.hpp>
#include <Unreal/FProperty.hpp>
#include "kh3ap/app/enums.h"
#include "kh3ap/features/player/state/sora_pawn_catalog.h"
#include <Unreal/Property/FBoolProperty.hpp>
#include <algorithm>
#include <cstring>
#include <format>

namespace kh3ap
{
    using namespace ability_probe_limits;
    auto prepare_live_equipmentish_follow_property_entries(
            RC::Unreal::UObject* followed_object,
            int max_entries) -> std::vector<PropertyProbeEntry>
    {
        std::vector<PropertyProbeEntry> entries{};
        if (!followed_object || !kh3ap::is_uobject_valid_fast(followed_object) || max_entries <= 0)
        {
            return entries;
        }

        RC::Unreal::UClass* followed_class = followed_object->GetClassPrivate();
        if (!followed_class)
        {
            return entries;
        }

        const std::wstring object_name = followed_object->GetFullName();
        const std::wstring class_name = followed_class->GetName();
        int followed_property_count = 0;
        for (RC::Unreal::FProperty* followed_property : followed_class->ForEachPropertyInChain())
        {
            if (!followed_property)
            {
                continue;
            }

            const std::wstring followed_property_name = followed_property->GetName();
            const std::wstring followed_property_class = followed_property->GetClass().GetName();
            if (!should_probe_live_equipmentish_property(followed_property_name, followed_property_class))
            {
                continue;
            }

            const int followed_offset = followed_property->GetOffset_Internal();
            const int followed_element_size = followed_property->GetElementSize();
            if (followed_offset < 0 || followed_element_size <= 0)
            {
                continue;
            }

            const auto* followed_bytes = reinterpret_cast<const std::byte*>(followed_object);
            const void* followed_value_ptr = followed_bytes + followed_offset;
            const std::wstring followed_value = followed_property_class == STR("ArrayProperty")
                                                                ? format_live_ability_owner_array_value(followed_property, followed_value_ptr)
                                                                : format_ability_state_property_value(
                                                                          followed_object,
                                                                          followed_property,
                                                                          followed_value_ptr,
                                                                          followed_element_size);
            entries.push_back(PropertyProbeEntry{
                    .kind = STR("follow_property"),
                    .object_name = object_name,
                    .class_name = class_name,
                    .property_name = followed_property_name,
                    .property_class = followed_property_class,
                    .offset = followed_offset,
                    .element_size = followed_element_size,
                    .value = truncate_for_log(followed_value, followed_value_log_chars),
            });

            ++followed_property_count;
            if (followed_property_count >= max_entries)
            {
                break;
            }
        }

        return entries;
    }

    auto prepare_live_ability_owner_property_probe_entries(
            RC::Unreal::UObject* object,
            int max_entries) -> std::vector<LiveAbilityOwnerPropertyProbeEntry>
    {
        std::vector<LiveAbilityOwnerPropertyProbeEntry> entries{};
        if (!object || !kh3ap::is_uobject_valid_fast(object) || max_entries <= 0)
        {
            return entries;
        }

        RC::Unreal::UClass* object_class = object->GetClassPrivate();
        if (!object_class)
        {
            return entries;
        }

        const bool is_equipmentish_object = is_equipmentish_ability_owner_object(object);
        const std::wstring object_name = object->GetFullName();
        const std::wstring class_name = object_class->GetName();
        int logged_property_count = 0;
        for (RC::Unreal::FProperty* property : object_class->ForEachPropertyInChain())
        {
            if (!property)
            {
                continue;
            }

            const std::wstring property_name = property->GetName();
            const std::wstring property_class = property->GetClass().GetName();
            if (!(is_equipmentish_object
                          ? should_probe_live_equipmentish_property(property_name, property_class)
                          : should_probe_live_ability_owner_property(property_name, property_class)))
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

            RC::Unreal::UObject* followed_object = nullptr;
            if (property_class == STR("ObjectProperty") &&
                contains_any_wide(property_name, {STR("Equipment"), STR("Ability"), STR("Equip"), STR("MyEquipment")}) &&
                element_size >= static_cast<int>(sizeof(RC::Unreal::UObject*)))
            {
                std::memcpy(&followed_object, value_ptr, sizeof(RC::Unreal::UObject*));
                if (!is_equipmentish_ability_owner_object(followed_object))
                {
                    followed_object = nullptr;
                }
            }

            entries.push_back(LiveAbilityOwnerPropertyProbeEntry{
                    .entry = PropertyProbeEntry{
                            .kind = STR("property"),
                            .object_name = object_name,
                            .class_name = class_name,
                            .property_name = property_name,
                            .property_class = property_class,
                            .offset = offset,
                            .element_size = element_size,
                            .value = truncate_for_log(value, followed_value_log_chars),
                    },
                    .raw_value = format_bytes_hex(value_ptr, static_cast<size_t>(std::clamp(element_size, 1, state_property_preview_bytes))),
                    .followed_object = followed_object,
            });

            ++logged_property_count;
            if (logged_property_count >= max_entries)
            {
                break;
            }
        }

        return entries;
    }

    auto prepare_live_ability_owner_probe_candidate_data(
            RC::Unreal::UObject* preferred_object,
            RC::Unreal::UObject* last_world_context_object,
            int max_objects) -> LiveAbilityOwnerProbeCandidateData
    {
        LiveAbilityOwnerProbeCandidateData data{};
        if (max_objects <= 0)
        {
            return data;
        }

        const auto candidates = collect_live_ability_owner_candidates(preferred_object, last_world_context_object);
        data.candidate_count = candidates.size();

        int object_count = 0;
        for (RC::Unreal::UObject* object : candidates)
        {
            if (!is_live_ability_owner_candidate(object))
            {
                continue;
            }

            RC::Unreal::UClass* object_class = object->GetClassPrivate();
            if (!object_class)
            {
                continue;
            }

            data.objects.push_back(LiveAbilityOwnerProbeObjectEntry{
                    .object = object,
                    .object_name = object->GetFullName(),
                    .class_name = object_class->GetName(),
                    .property_limit = is_equipmentish_ability_owner_object(object) ? max_equipment_owner_properties : max_other_owner_properties,
            });

            ++object_count;
            if (object_count >= max_objects)
            {
                break;
            }
        }

        return data;
    }

    auto format_targeted_sora_state_property_value(
            RC::Unreal::UObject* object,
            RC::Unreal::FProperty* property,
            const void* value_ptr,
            int element_size) -> std::wstring
    {
        if (!object || !property || !value_ptr || element_size <= 0)
        {
            return STR("<none>");
        }

        const std::wstring property_class = property->GetClass().GetName();
        if (property_class == STR("ArrayProperty"))
        {
            return format_live_ability_owner_array_value(property, value_ptr);
        }
        return format_ability_state_property_value(object, property, value_ptr, element_size);
    }

    auto prepare_targeted_reflected_state_property_entries(
            RC::Unreal::UObject* object,
            std::wstring_view object_role,
            int max_properties) -> std::vector<PropertyProbeEntry>
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
                .kind = std::wstring{object_role},
                .object_name = object_name,
                .class_name = class_name,
        });

        int logged_count = 0;
        for (RC::Unreal::FProperty* property : object_class->ForEachPropertyInChain())
        {
            if (!property)
            {
                continue;
            }

            const std::wstring property_name = property->GetName();
            const std::wstring property_class = property->GetClass().GetName();
            if (!should_log_targeted_sora_state_property(property_name, property_class))
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
            entries.push_back(PropertyProbeEntry{
                    .kind = STR("property"),
                    .object_name = object_name,
                    .class_name = class_name,
                    .property_name = property_name,
                    .property_class = property_class,
                    .offset = offset,
                    .element_size = element_size,
                    .value = truncate_for_log(format_targeted_sora_state_property_value(object, property, value_ptr, element_size), targeted_value_log_chars),
            });

            ++logged_count;
            if (logged_count >= max_properties)
            {
                break;
            }
        }

        return entries;
    }

    auto object_has_ability_discovery_interest(RC::Unreal::UObject* object) -> bool
    {
        if (!object || !kh3ap::is_uobject_valid_fast(object))
        {
            return false;
        }
        const std::wstring object_name = object->GetFullName();
        const std::wstring class_name = object->GetClassPrivate() ? object->GetClassPrivate()->GetName() : std::wstring{};
        return contains_any_wide(object_name, {STR("Ability"), STR("Bonus"), STR("Equip"), STR("Save"), STR("Player"), STR("Sora"), STR("Randomizer")}) ||
               runtime_sora_pawn_catalog_matches_any(object_name, class_name) ||
               contains_any_wide(class_name, {STR("Ability"), STR("Bonus"), STR("Equip"), STR("Save"), STR("Player"), STR("Data"), STR("Randomizer")});
    }

    auto format_ability_state_property_value(
            RC::Unreal::UObject* object,
            RC::Unreal::FProperty* property,
            const void* value_ptr,
            int element_size) -> std::wstring
    {
        if (!object || !property || !value_ptr || element_size <= 0)
        {
            return STR("<none>");
        }

        const std::wstring property_name = property->GetName();
        const std::wstring property_class = property->GetClass().GetName();
        if (property_class == STR("NameProperty"))
        {
            if (const auto value = try_read_object_name_property(object, property_name.c_str()); value.has_value())
            {
                return *value;
            }
        }
        if (property_class == STR("StrProperty"))
        {
            if (const auto value = try_read_object_string_property(object, property_name.c_str()); value.has_value())
            {
                return *value;
            }
        }
        if (property_class == STR("ObjectProperty"))
        {
            RC::Unreal::UObject* object_value = nullptr;
            if (element_size >= static_cast<int>(sizeof(RC::Unreal::UObject*)))
            {
                std::memcpy(&object_value, value_ptr, sizeof(RC::Unreal::UObject*));
            }
            if (object_value && kh3ap::is_uobject_valid_fast(object_value))
            {
                return format_object_debug_name(object_value);
            }
            if (object_value)
            {
                return std::format(STR("ptr=0x{:X} real=false"), reinterpret_cast<uintptr_t>(object_value));
            }
            return STR("ptr=0x0");
        }
        if (property_class == STR("BoolProperty"))
        {
            if (const auto value = try_read_object_bool_property(object, property_name.c_str()); value.has_value())
            {
                return *value ? STR("true") : STR("false");
            }
        }
        if (property_class == STR("ArrayProperty"))
        {
            ArrayHeader header{};
            std::memcpy(&header, value_ptr, sizeof(ArrayHeader));
            std::wostringstream stream;
            stream << STR("num=") << header.num << STR(" max=") << header.max << STR(" ptr=") << header.data_ptr;
            if (const auto entries = try_format_supported_array_entries(property, value_ptr); entries.has_value())
            {
                stream << STR(" entries=") << *entries;
            }
            return stream.str();
        }
        if (property_class == STR("MapProperty"))
        {
            MapHeader header{};
            std::memcpy(&header, value_ptr, sizeof(MapHeader));
            std::wostringstream stream;
            stream << STR("num=") << header.num << STR(" max=") << header.max << STR(" ptr=") << header.data_ptr;
            if (const auto entries = try_format_supported_map_entries(property, value_ptr); entries.has_value())
            {
                stream << STR(" entries=") << *entries;
            }
            return stream.str();
        }
        if (property_class == STR("StructProperty"))
        {
            return std::format(STR("raw={}"), format_bytes_hex(value_ptr, static_cast<size_t>(std::clamp(element_size, 1, state_struct_preview_bytes))));
        }

        const uint64_t raw_value = read_unsigned_param_value(value_ptr, element_size);
        if (property_name.find(STR("Ability")) != std::wstring_view::npos)
        {
            const std::wstring ability_name = try_format_runtime_ability_enum_value(raw_value);
            if (!ability_name.empty())
            {
                return std::format(STR("{} ({})"), raw_value, ability_name);
            }
        }
        return std::to_wstring(raw_value);
    }

    auto prepare_ability_discovery_snapshot_entries(
            const std::vector<RC::Unreal::UObject*>& candidates,
            int max_objects,
            int max_properties_per_object) -> std::vector<AbilityDiscoveryObjectProbeEntry>
    {
        std::vector<AbilityDiscoveryObjectProbeEntry> entries{};
        if (max_objects <= 0 || max_properties_per_object <= 0)
        {
            return entries;
        }

        int object_count = 0;
        for (RC::Unreal::UObject* object : candidates)
        {
            if (!object_has_ability_discovery_interest(object))
            {
                continue;
            }
            RC::Unreal::UClass* object_class = object->GetClassPrivate();
            if (!object_class)
            {
                continue;
            }

            const std::wstring object_name = object->GetFullName();
            const std::wstring class_name = object_class->GetName();
            AbilityDiscoveryObjectProbeEntry object_entry{
                    .object_name = object_name,
                    .class_name = class_name,
            };

            int logged_property_count = 0;
            for (RC::Unreal::FProperty* property : object_class->ForEachPropertyInChain())
            {
                if (!property)
                {
                    continue;
                }

                const std::wstring property_name = property->GetName();
                const std::wstring property_class = property->GetClass().GetName();
                if (!should_probe_ability_state_property(property_name, property_class))
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
                object_entry.properties.push_back(AbilityDiscoveryPropertyProbeEntry{
                        .entry = PropertyProbeEntry{
                                .kind = STR("property"),
                                .object_name = object_name,
                                .class_name = class_name,
                                .property_name = property_name,
                                .property_class = property_class,
                                .offset = offset,
                                .element_size = element_size,
                                .value = truncate_for_log(format_ability_state_property_value(object, property, value_ptr, element_size), scalar_value_log_chars),
                        },
                        .raw_value = format_bytes_hex(value_ptr, static_cast<size_t>(std::clamp(element_size, 1, state_property_preview_bytes))),
                });

                ++logged_property_count;
                if (logged_property_count >= max_properties_per_object)
                {
                    break;
                }
            }

            entries.push_back(std::move(object_entry));
            ++object_count;
            if (object_count >= max_objects)
            {
                break;
            }
        }

        return entries;
    }
}
