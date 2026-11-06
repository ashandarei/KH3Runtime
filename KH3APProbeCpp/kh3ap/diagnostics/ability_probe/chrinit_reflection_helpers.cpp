#include "kh3ap/bridge/transport/bridge_channels.h"
#include "kh3ap/diagnostics/ability_probe/chrinit_reflection_helpers.h"
#include "kh3ap/core/reflection/reflection_core.h"

#include "kh3ap/diagnostics/output/diagnostic_once_set.h"
#include "kh3ap/diagnostics/output/diagnostic_probe_serializer.h"
#include "kh3ap/features/grants/messages/ap_message_param_storage.h"
#include "kh3ap/hooks/blocking_ui/blocking_ui_state.h"
#include "kh3ap/hooks/hook_filters.h"
#include "kh3ap/app/enums.h"
#include "kh3ap/features/player/objects/object_resolver.h"
#include "kh3ap/features/player/state/sora_pawn_catalog.h"
#include "kh3ap/app/state.h"
#include "kh3ap/bridge/utilities.h"

#include <UnrealDef.hpp>
#include <Unreal/FFrame.hpp>
#include <Unreal/FString.hpp>
#include <Unreal/FText.hpp>
#include <Unreal/NameTypes.hpp>
#include <Unreal/Property/FArrayProperty.hpp>
#include <Unreal/Property/FBoolProperty.hpp>
#include <Unreal/Property/FMapProperty.hpp>
#include <Unreal/Property/FStructProperty.hpp>
#include <Unreal/TMap.hpp>
#include <Unreal/UObjectGlobals.hpp>
#include <Unreal/UScriptStruct.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <cwctype>
#include <format>
#include <mutex>
#include <set>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#define NOMINMAX
#include <windows.h>

namespace kh3ap
{
    namespace
    {
        constexpr auto chrinit_entry_log_chars = 320;
        constexpr auto chrinit_field_log_chars = 260;
        constexpr uint8_t thunder_up_ability_id = 0x23;

        constexpr auto max_chrinit_owner_candidates = 8;
        constexpr auto max_chrinit_ability_entries = 256;
        constexpr auto max_chrinit_form_entries = 64;
        constexpr auto max_chrinit_nested_entries = 128;
        constexpr auto max_chrinit_scalar_bytes = 8;
        constexpr auto max_chrinit_nested_element_bytes = 16;
        constexpr auto chrinit_ability_sample_count = 16;
        constexpr auto chrinit_form_sample_count = 12;
        constexpr auto chrinit_property_log_limit = 80;
        constexpr auto chrinit_byte_sample_limit = 32;
        constexpr auto chrinit_field_byte_sample_limit = 24;
        auto format_chrinit_enum_value(uint64_t raw_value) -> std::wstring
        {
            // Keep ChrInit probing raw-only. Resolving enum display names inside
            // this post-load hook can hit UE4SS's callback string limit.
            return std::to_wstring(raw_value);
        }
    } // namespace

    auto collect_targeted_chrinit_candidates() -> std::vector<RC::Unreal::UObject*>
    {
        std::vector<RC::Unreal::UObject*> candidates{};

        const std::array<std::wstring_view, 1> class_names{
            STR("TresChrInitData"),
        };
        for (std::wstring_view class_name : class_names)
        {
            std::vector<RC::Unreal::UObject*> objects{};
            RC::Unreal::UObjectGlobals::FindAllOf(class_name, objects);
            for (RC::Unreal::UObject* object : objects)
            {
                if (!object || !kh3ap::is_uobject_valid_fast(object))
                {
                    continue;
                }
                const std::wstring full_name = object->GetFullName();
                if (full_name.find(STR("TresChrInitData")) == std::wstring::npos &&
                    full_name.find(STR("ChrInit")) == std::wstring::npos)
                {
                    continue;
                }
                if (std::find(candidates.begin(), candidates.end(), object) == candidates.end())
                {
                    candidates.push_back(object);
                }
                if (candidates.size() >= max_chrinit_owner_candidates)
                {
                    return candidates;
                }
            }
        }

        return candidates;
    }

    auto format_form_ability_enum_array(const ArrayHeader& header, int element_size) -> std::wstring
    {
        if (!header.data_ptr || header.num <= 0 || header.num > max_chrinit_ability_entries ||
            element_size <= 0 || element_size > max_chrinit_scalar_bytes)
        {
            return std::format(STR("num={} max={} ptr=0x{:X} entries=<unavailable>"), header.num,
                               header.max, header.data_ptr);
        }

        std::wostringstream stream;
        stream << STR("num=") << header.num << STR(" max=") << header.max << STR(" ptr=0x")
               << std::hex << header.data_ptr << std::dec << STR(" entries=[");
        const auto* base =
            reinterpret_cast<const std::byte*>(static_cast<uintptr_t>(header.data_ptr));
        const int count = std::min(header.num, chrinit_ability_sample_count);
        for (int index = 0; index < count; ++index)
        {
            if (index > 0)
            {
                stream << STR(", ");
            }

            uint64_t raw_value = 0;
            std::memcpy(&raw_value,
                        base + (static_cast<size_t>(index) * static_cast<size_t>(element_size)),
                        static_cast<size_t>(element_size));
            std::wstring ability_name = try_format_runtime_ability_enum_value(raw_value);
            stream << raw_value;
            if (!ability_name.empty())
            {
                stream << STR(":") << ability_name;
            }
        }
        if (header.num > count)
        {
            stream << STR(", ...");
        }
        stream << STR("]");
        return stream.str();
    }

    auto format_compact_form_ability_enum_array(const ArrayHeader& header, int element_size)
        -> std::wstring
    {
        if (!header.data_ptr || header.num <= 0 || header.num > max_chrinit_form_entries ||
            element_size <= 0 || element_size > max_chrinit_scalar_bytes)
        {
            return std::format(STR("n={} max={} entries=<none>"), header.num, header.max);
        }

        std::wostringstream stream;
        stream << STR("n=") << header.num << STR(" [");
        const auto* base =
            reinterpret_cast<const std::byte*>(static_cast<uintptr_t>(header.data_ptr));
        const int count = std::min(header.num, chrinit_form_sample_count);
        for (int index = 0; index < count; ++index)
        {
            if (index > 0)
            {
                stream << STR(",");
            }

            uint64_t raw_value = 0;
            std::memcpy(&raw_value,
                        base + (static_cast<size_t>(index) * static_cast<size_t>(element_size)),
                        static_cast<size_t>(element_size));
            std::wstring ability_name = try_format_runtime_ability_enum_value(raw_value);
            constexpr std::wstring_view prefix = STR("ETresAbilityKind::");
            if (ability_name.starts_with(prefix))
            {
                ability_name = ability_name.substr(prefix.size());
            }
            stream << (ability_name.empty() ? std::to_wstring(raw_value) : ability_name);
        }
        if (header.num > count)
        {
            stream << STR(",...");
        }
        stream << STR("]");
        return stream.str();
    }

    auto form_ability_array_contains_value(const ArrayHeader& header, int element_size,
                                           uint64_t target_value) -> bool
    {
        if (!header.data_ptr || header.num <= 0 || header.num > max_chrinit_ability_entries ||
            element_size <= 0 || element_size > max_chrinit_scalar_bytes)
        {
            return false;
        }

        const auto* base =
            reinterpret_cast<const std::byte*>(static_cast<uintptr_t>(header.data_ptr));
        for (int index = 0; index < header.num; ++index)
        {
            uint64_t raw_value = 0;
            std::memcpy(&raw_value,
                        base + (static_cast<size_t>(index) * static_cast<size_t>(element_size)),
                        static_cast<size_t>(element_size));
            if (raw_value == target_value)
            {
                return true;
            }
        }
        return false;
    }

    auto format_chrinit_name_value(const void* value_ptr, int element_size) -> std::wstring
    {
        if (!value_ptr || element_size < static_cast<int>(sizeof(uint64_t)))
        {
            return STR("<unavailable>");
        }

        uint64_t raw_name = 0;
        std::memcpy(&raw_name, value_ptr, sizeof(uint64_t));
        const auto decoded = decode_name_value(raw_name);
        const std::wstring& candidate =
            !decoded.fname_pair.empty() && decoded.fname_pair != STR("None") ? decoded.fname_pair
                                                                             : decoded.fname_first;
        return candidate.empty() ? std::format(STR("raw_{}"), raw_name) : candidate;
    }

    auto log_chrinit_array_value(std::wstring_view object_name, std::wstring_view struct_name,
                                 std::wstring_view field_name, RC::Unreal::FProperty* property,
                                 const void* value_ptr) -> void
    {
        if (!property || !value_ptr || property->GetClass().GetName() != STR("ArrayProperty"))
        {
            return;
        }

        auto* array_property = static_cast<RC::Unreal::FArrayProperty*>(property);
        RC::Unreal::FProperty* inner_property =
            const_cast<RC::Unreal::FProperty*>(array_property->GetInner());
        const std::wstring inner_class =
            inner_property ? inner_property->GetClass().GetName() : STR("<none>");
        const int inner_size = inner_property ? inner_property->GetElementSize() : 0;

        ArrayHeader header{};
        std::memcpy(&header, value_ptr, sizeof(ArrayHeader));

        std::wostringstream entries;
        entries << STR("[");
        if (header.data_ptr && header.num > 0 && header.num <= max_chrinit_nested_entries &&
            inner_size > 0 && inner_size <= max_chrinit_nested_element_bytes)
        {
            const auto* base =
                reinterpret_cast<const std::byte*>(static_cast<uintptr_t>(header.data_ptr));
            const int count = std::min(header.num, chrinit_form_sample_count);
            for (int index = 0; index < count; ++index)
            {
                if (index > 0)
                {
                    entries << STR(",");
                }

                const void* entry_ptr =
                    base + (static_cast<size_t>(index) * static_cast<size_t>(inner_size));
                if (inner_class == STR("NameProperty"))
                {
                    entries << format_chrinit_name_value(entry_ptr, inner_size);
                }
                else
                {
                    const uint64_t raw_value = read_unsigned_param_value(entry_ptr, inner_size);
                    entries << format_chrinit_enum_value(raw_value);
                }
            }
            if (header.num > count)
            {
                entries << STR(",...");
            }
        }
        entries << STR("]");

        const auto* bytes_base =
            header.data_ptr
                ? reinterpret_cast<const std::byte*>(static_cast<uintptr_t>(header.data_ptr))
                : nullptr;
        const int byte_count =
            (bytes_base && header.num > 0 && header.num <= max_chrinit_nested_entries &&
             inner_size > 0 && inner_size <= max_chrinit_nested_element_bytes)
                ? std::clamp(header.num * inner_size, 1, chrinit_byte_sample_limit)
                : 0;

        RC::Output::send<RC::LogLevel::Warning>(
            STR("[KH3APProbeCpp] ChrInitProbeArray object_class={} struct={} field={} "
                "inner_class={} inner_size={} num={} max={} ptr=0x{:X} bytes={} entries={}\n"),
            STR("TresChrInitData"), struct_name, field_name, inner_class, inner_size, header.num,
            header.max, header.data_ptr,
            bytes_base ? format_bytes_hex(bytes_base, static_cast<size_t>(byte_count))
                       : std::wstring{STR("<none>")},
            truncate_for_log(entries.str(), chrinit_entry_log_chars));
    }

    auto format_chrinit_field_value(RC::Unreal::FProperty* property, const void* value_ptr)
        -> std::wstring
    {
        if (!property || !value_ptr)
        {
            return STR("<none>");
        }

        const std::wstring property_class = property->GetClass().GetName();
        const int element_size = property->GetElementSize();
        if (property_class == STR("NameProperty"))
        {
            return format_chrinit_name_value(value_ptr, element_size);
        }
        if (property_class == STR("ArrayProperty"))
        {
            ArrayHeader header{};
            std::memcpy(&header, value_ptr, sizeof(ArrayHeader));
            return std::format(STR("array num={} max={} ptr=0x{:X}"), header.num, header.max,
                               header.data_ptr);
        }
        if (property_class == STR("BoolProperty"))
        {
            return read_unsigned_param_value(value_ptr, std::min(element_size, 1)) != 0
                       ? STR("true")
                       : STR("false");
        }
        if (property_class == STR("ObjectProperty"))
        {
            RC::Unreal::UObject* object_value = nullptr;
            if (element_size >= static_cast<int>(sizeof(RC::Unreal::UObject*)))
            {
                std::memcpy(&object_value, value_ptr, sizeof(RC::Unreal::UObject*));
            }
            return object_value && kh3ap::is_uobject_valid_fast(object_value)
                       ? format_object_debug_name(object_value)
                       : std::format(STR("ptr=0x{:X}"), reinterpret_cast<uintptr_t>(object_value));
        }
        if (property_class == STR("StructProperty"))
        {
            auto* struct_property = static_cast<RC::Unreal::FStructProperty*>(property);
            RC::Unreal::UScriptStruct* script_struct = struct_property->GetStruct();
            return std::format(
                STR("struct={} raw={}"),
                script_struct ? script_struct->GetName() : std::wstring{STR("<null>")},
                format_bytes_hex(value_ptr,
                                 static_cast<size_t>(std::clamp(element_size, 1,
                                                                chrinit_field_byte_sample_limit))));
        }

        const uint64_t raw_value = read_unsigned_param_value(value_ptr, element_size);
        if (property->GetName().find(STR("Ability")) != std::wstring::npos)
        {
            return format_chrinit_enum_value(raw_value);
        }
        return std::to_wstring(raw_value);
    }

    auto log_chrinit_struct_value(bool thunder_patch_enabled, RC::Unreal::UObject* object,
                                  std::wstring_view owner_property_name,
                                  RC::Unreal::FStructProperty* struct_property,
                                  void* struct_value_ptr) -> void
    {
        if (!object || !struct_property || !struct_value_ptr)
        {
            return;
        }

        RC::Unreal::UScriptStruct* script_struct = struct_property->GetStruct();
        const std::wstring struct_name = script_struct ? script_struct->GetName() : STR("<null>");
        int nested_count = 0;
        if (!script_struct)
        {
            return;
        }

        for (RC::Unreal::FProperty* nested_property : script_struct->ForEachPropertyInChain())
        {
            if (!nested_property)
            {
                continue;
            }

            const int offset = nested_property->GetOffset_Internal();
            const int element_size = nested_property->GetElementSize();
            if (offset < 0 || element_size <= 0)
            {
                continue;
            }

            auto* nested_value_ptr = static_cast<std::byte*>(struct_value_ptr) + offset;
            const std::wstring nested_class = nested_property->GetClass().GetName();
            std::wstring inner_class{STR("<none>")};
            if (nested_class == STR("ArrayProperty"))
            {
                auto* array_property = static_cast<RC::Unreal::FArrayProperty*>(nested_property);
                if (RC::Unreal::FProperty* inner_property =
                        const_cast<RC::Unreal::FProperty*>(array_property->GetInner()))
                {
                    inner_class = inner_property->GetClass().GetName();
                }
            }

            RC::Output::send<RC::LogLevel::Warning>(
                STR("[KH3APProbeCpp] ChrInitProbeStructField object={} struct={} field={} class={} "
                    "inner_class={} offset={} elem_size={} value={}\n"),
                object->GetFullName(), struct_name, nested_property->GetName(), nested_class,
                inner_class, offset, element_size,
                truncate_for_log(format_chrinit_field_value(nested_property, nested_value_ptr),
                                 chrinit_field_log_chars));

            if (nested_class == STR("ArrayProperty"))
            {
                const auto patch_status = maybe_patch_chrinit_player_sora_thunder(
                    thunder_patch_enabled, owner_property_name, nested_property->GetName(),
                    nested_property, nested_value_ptr);
                if (patch_status != STR("skipped"))
                {
                    RC::Output::send<RC::LogLevel::Warning>(
                        STR("[KH3APProbeCpp] ChrInitPatch owner={} field={} target=THUNDER_UP "
                            "status={}\n"),
                        owner_property_name, nested_property->GetName(), patch_status);
                }
                log_chrinit_array_value(object->GetFullName(), struct_name,
                                        nested_property->GetName(), nested_property,
                                        nested_value_ptr);
            }

            ++nested_count;
            if (nested_count >= chrinit_property_log_limit)
            {
                break;
            }
        }

        RC::Output::send<RC::LogLevel::Warning>(
            STR("[KH3APProbeCpp] ChrInitProbeStructSummary object={} struct={} fields_logged={}\n"),
            object->GetFullName(), struct_name, nested_count);
    }

    auto log_targeted_chrinit_probe(bool enabled, bool thunder_patch_enabled,
                                    std::wstring_view reason, std::wstring_view runtime_context)
        -> void
    {
        if (!enabled)
        {
            return;
        }

        const auto candidates = collect_targeted_chrinit_candidates();
        RC::Output::send<RC::LogLevel::Warning>(
            STR("[KH3APProbeCpp] ChrInitProbe reason={} candidates={} context={}\n"), reason,
            static_cast<uint64_t>(candidates.size()), runtime_context);

        for (RC::Unreal::UObject* object : candidates)
        {
            if (!object || !kh3ap::is_uobject_valid_fast(object))
            {
                continue;
            }
            auto* object_class = object->GetClassPrivate();
            if (!object_class)
            {
                continue;
            }

            int logged_count = 0;
            int seen_count = 0;
            for (RC::Unreal::FProperty* property : object_class->ForEachPropertyInChain())
            {
                if (!property)
                {
                    continue;
                }

                ++seen_count;
                const std::wstring property_name = property->GetName();
                const std::wstring property_class = property->GetClass().GetName();
                const int offset = property->GetOffset_Internal();
                const int element_size = property->GetElementSize();
                if (offset < 0 || element_size <= 0)
                {
                    continue;
                }

                const auto* value_ptr = reinterpret_cast<const std::byte*>(object) + offset;
                std::wstring struct_name{STR("<none>")};
                if (property_class == STR("StructProperty"))
                {
                    auto* struct_property = static_cast<RC::Unreal::FStructProperty*>(property);
                    if (RC::Unreal::UScriptStruct* script_struct = struct_property->GetStruct())
                    {
                        struct_name = script_struct->GetName();
                    }
                }

                RC::Output::send<RC::LogLevel::Warning>(
                    STR("[KH3APProbeCpp] ChrInitProbeProperty object={} class={} property={} "
                        "prop_class={} struct={} offset={} elem_size={} value={}\n"),
                    object->GetFullName(), object_class->GetName(), property_name, property_class,
                    struct_name, offset, element_size,
                    truncate_for_log(format_chrinit_field_value(property, value_ptr),
                                     chrinit_field_log_chars));

                if (property_class == STR("StructProperty") &&
                    (property_name.find(STR("Sora")) != std::wstring::npos ||
                     property_name == STR("m_PlayerSora") ||
                     struct_name.find(STR("ChrInit")) != std::wstring::npos))
                {
                    log_chrinit_struct_value(thunder_patch_enabled, object, property_name,
                                             static_cast<RC::Unreal::FStructProperty*>(property),
                                             const_cast<std::byte*>(value_ptr));
                }

                ++logged_count;
                if (logged_count >= chrinit_property_log_limit)
                {
                    break;
                }
            }

            RC::Output::send<RC::LogLevel::Warning>(
                STR("[KH3APProbeCpp] ChrInitProbePropertySummary object={} class={} seen={} "
                    "logged={}\n"),
                object->GetFullName(), object_class->GetName(), seen_count, logged_count);
        }
    }

    auto try_append_form_ability_byte(ArrayHeader& header, int element_size, uint8_t target_value)
        -> std::wstring_view
    {
        if (!header.data_ptr || header.num < 0 || header.max <= 0 || header.num > header.max ||
            header.max > max_chrinit_form_entries || element_size != 1)
        {
            return STR("invalid");
        }

        auto* base = reinterpret_cast<std::byte*>(static_cast<uintptr_t>(header.data_ptr));
        for (int index = 0; index < header.num; ++index)
        {
            if (std::to_integer<uint8_t>(base[index]) == target_value)
            {
                return STR("already_present");
            }
        }

        if (header.num >= header.max)
        {
            return STR("full");
        }

        base[header.num] = static_cast<std::byte>(target_value);
        ++header.num;
        return STR("appended");
    }

    auto maybe_patch_chrinit_player_sora_thunder(bool enabled,
                                                 std::wstring_view owner_property_name,
                                                 std::wstring_view field_name,
                                                 RC::Unreal::FProperty* property, void* value_ptr)
        -> std::wstring_view
    {
        if (!enabled)
        {
            return STR("disabled");
        }

        if (owner_property_name != STR("m_PlayerSora") ||
            (field_name != STR("m_HaveAbility") && field_name != STR("m_EquipAbility")))
        {
            return STR("skipped");
        }
        if (!property || !value_ptr || property->GetClass().GetName() != STR("ArrayProperty"))
        {
            return STR("invalid");
        }

        auto* array_property = static_cast<RC::Unreal::FArrayProperty*>(property);
        RC::Unreal::FProperty* inner_property =
            const_cast<RC::Unreal::FProperty*>(array_property->GetInner());
        const int inner_size = inner_property ? inner_property->GetElementSize() : 0;
        auto* header = static_cast<ArrayHeader*>(value_ptr);

        // ETresAbilityKind::THUNDER_UP = 35 / 0x23 in the live UE4SS object dump.
        return try_append_form_ability_byte(*header, inner_size, thunder_up_ability_id);
    }
} // namespace kh3ap
