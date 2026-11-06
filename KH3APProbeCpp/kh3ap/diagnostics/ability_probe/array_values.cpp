#include "kh3ap/diagnostics/ability_probe/limits.h"
#include "kh3ap/diagnostics/ability_probe/ability_reflection_probe_helpers.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/bridge/utilities.h"
#include <Unreal/UObject.hpp>
#include <Unreal/UClass.hpp>
#include <Unreal/FProperty.hpp>
#include "kh3ap/app/enums.h"
#include <Unreal/Property/FArrayProperty.hpp>
#include <Unreal/Property/FMapProperty.hpp>
#include <algorithm>
#include <array>
#include <cstring>
#include <set>
#include <sstream>

namespace kh3ap
{
    using namespace ability_probe_limits;
    namespace
    {
        struct AbilityArrayNeedle
        {
            uint64_t value{};
            std::wstring_view name{};
        };

        constexpr std::array<AbilityArrayNeedle, 8> s_ability_array_needles{{
                {0x03, STR("REFLECT_GUARD")},
                {0x13, STR("SUPERSLIDE")},
                {0x15, STR("LIBRA")},
                {0x16, STR("DODGE")},
                {0x17, STR("AIRSLIDE")},
                {0x18, STR("AIRDODGE")},
                {0x1A, STR("EXPZERO")},
                {0x23, STR("THUNDER_UP")},
        }};
    }

    auto format_live_ability_owner_array_value(
            RC::Unreal::FProperty* property,
            const void* value_ptr) -> std::wstring
    {
        if (!property || !value_ptr || property->GetClass().GetName() != STR("ArrayProperty"))
        {
            return STR("<not_array>");
        }

        const auto* array_property = static_cast<RC::Unreal::FArrayProperty*>(property);
        RC::Unreal::FProperty* inner_property = const_cast<RC::Unreal::FProperty*>(array_property->GetInner());
        const std::wstring inner_class = inner_property ? inner_property->GetClass().GetName() : std::wstring{STR("<none>")};
        const int inner_size = inner_property ? inner_property->GetElementSize() : 0;

        ArrayHeader header{};
        std::memcpy(&header, value_ptr, sizeof(ArrayHeader));
        std::wostringstream stream;
        stream << STR("inner_class=") << inner_class
               << STR(" inner_size=") << inner_size
               << STR(" num=") << header.num
               << STR(" max=") << header.max
               << STR(" ptr=0x") << std::hex << header.data_ptr << std::dec;

        bool contains_thunder_up = false;
        if (header.data_ptr && header.num > 0 && header.num <= max_live_owner_array_entries && inner_size > 0 && inner_size <= max_scalar_bytes)
        {
            const auto* base = reinterpret_cast<const std::byte*>(static_cast<uintptr_t>(header.data_ptr));
            const int entry_count = std::min(header.num, max_live_owner_preview_entries);
            stream << STR(" entries=[");
            for (int index = 0; index < entry_count; ++index)
            {
                if (index > 0)
                {
                    stream << STR(", ");
                }

                uint64_t raw_value = 0;
                std::memcpy(&raw_value, base + (static_cast<size_t>(index) * static_cast<size_t>(inner_size)), static_cast<size_t>(inner_size));
                if (inner_size == 1 && raw_value == thunder_up_ability_id)
                {
                    contains_thunder_up = true;
                }
                stream << raw_value;
                if (const std::wstring ability_name = try_format_runtime_ability_enum_value(raw_value); !ability_name.empty())
                {
                    stream << STR(":") << ability_name;
                }
            }
            if (header.num > entry_count)
            {
                stream << STR(", ...");
            }
            stream << STR("]");

            const int byte_count = std::min(header.num * inner_size, live_owner_preview_bytes);
            stream << STR(" bytes=") << format_bytes_hex(base, static_cast<size_t>(byte_count));
        }

        stream << STR(" has_THUNDER_UP=") << (contains_thunder_up ? STR("true") : STR("false"));
        return stream.str();
    }

    auto try_get_ability_array_needle_name(uint64_t raw_value) -> std::wstring_view
    {
        for (const AbilityArrayNeedle& needle : s_ability_array_needles)
        {
            if (needle.value == raw_value)
            {
                return needle.name;
            }
        }
        return {};
    }

    auto try_format_ability_array_needle_value(
            RC::Unreal::FProperty* property,
            const void* value_ptr) -> std::optional<std::pair<std::wstring, std::wstring>>
    {
        if (!property || !value_ptr || property->GetClass().GetName() != STR("ArrayProperty"))
        {
            return std::nullopt;
        }

        const auto* array_property = static_cast<RC::Unreal::FArrayProperty*>(property);
        RC::Unreal::FProperty* inner_property = const_cast<RC::Unreal::FProperty*>(array_property->GetInner());
        if (!inner_property)
        {
            return std::nullopt;
        }

        const std::wstring inner_class = inner_property->GetClass().GetName();
        const int inner_size = inner_property->GetElementSize();
        if (inner_size <= 0 || inner_size > max_scalar_bytes ||
            (inner_class != STR("ByteProperty") && inner_class != STR("EnumProperty")))
        {
            return std::nullopt;
        }

        ArrayHeader header{};
        std::memcpy(&header, value_ptr, sizeof(ArrayHeader));
        if (!header.data_ptr || header.num <= 0 || header.num > max_needle_array_entries || header.max < header.num)
        {
            return std::nullopt;
        }

        const auto* base = reinterpret_cast<const std::byte*>(static_cast<uintptr_t>(header.data_ptr));
        std::set<std::wstring> matched_names{};
        std::wostringstream entries;
        const int entry_count = std::min(header.num, max_needle_preview_entries);
        entries << STR("[");
        for (int index = 0; index < header.num; ++index)
        {
            uint64_t raw_value = 0;
            std::memcpy(&raw_value, base + (static_cast<size_t>(index) * static_cast<size_t>(inner_size)), static_cast<size_t>(inner_size));
            if (std::wstring_view matched = try_get_ability_array_needle_name(raw_value); !matched.empty())
            {
                matched_names.emplace(matched);
            }

            if (index < entry_count)
            {
                if (index > 0)
                {
                    entries << STR(", ");
                }
                entries << raw_value;
                if (std::wstring_view matched = try_get_ability_array_needle_name(raw_value); !matched.empty())
                {
                    entries << STR(":") << matched;
                }
            }
        }
        if (header.num > entry_count)
        {
            entries << STR(", ...");
        }
        entries << STR("]");

        if (matched_names.empty())
        {
            return std::nullopt;
        }

        std::wostringstream matches;
        bool first = true;
        for (const std::wstring& matched_name : matched_names)
        {
            if (!first)
            {
                matches << STR("|");
            }
            first = false;
            matches << matched_name;
        }

        const int byte_count = std::min(header.num * inner_size, needle_preview_bytes);
        std::wostringstream value;
        value << STR("inner_class=") << inner_class
              << STR(" inner_size=") << inner_size
              << STR(" num=") << header.num
              << STR(" max=") << header.max
              << STR(" ptr=0x") << std::hex << header.data_ptr << std::dec
              << STR(" entries=") << entries.str()
              << STR(" bytes=") << format_bytes_hex(base, static_cast<size_t>(byte_count));
        return std::make_pair(matches.str(), value.str());
    }

    auto try_format_manual_byte_ability_array_needle_value(
            const ArrayHeader& header) -> std::optional<std::pair<std::wstring, std::wstring>>
    {
        if (!header.data_ptr || header.num <= 0 || header.num > max_needle_array_entries || header.max < header.num)
        {
            return std::nullopt;
        }

        const auto* base = reinterpret_cast<const std::byte*>(static_cast<uintptr_t>(header.data_ptr));
        std::set<std::wstring> matched_names{};
        std::wostringstream entries;
        const int entry_count = std::min(header.num, max_needle_preview_entries);
        entries << STR("[");
        for (int index = 0; index < header.num; ++index)
        {
            const uint64_t raw_value = static_cast<uint64_t>(base[index]);
            if (std::wstring_view matched = try_get_ability_array_needle_name(raw_value); !matched.empty())
            {
                matched_names.emplace(matched);
            }

            if (index < entry_count)
            {
                if (index > 0)
                {
                    entries << STR(", ");
                }
                entries << raw_value;
                if (std::wstring_view matched = try_get_ability_array_needle_name(raw_value); !matched.empty())
                {
                    entries << STR(":") << matched;
                }
            }
        }
        if (header.num > entry_count)
        {
            entries << STR(", ...");
        }
        entries << STR("]");

        if (matched_names.empty())
        {
            return std::nullopt;
        }

        std::wostringstream matches;
        bool first = true;
        for (const std::wstring& matched_name : matched_names)
        {
            if (!first)
            {
                matches << STR("|");
            }
            first = false;
            matches << matched_name;
        }

        const int byte_count = std::min(header.num, needle_preview_bytes);
        std::wostringstream value;
        value << STR("manual_byte_array num=") << header.num
              << STR(" max=") << header.max
              << STR(" ptr=0x") << std::hex << header.data_ptr << std::dec
              << STR(" entries=") << entries.str()
              << STR(" bytes=") << format_bytes_hex(base, static_cast<size_t>(byte_count));
        return std::make_pair(matches.str(), value.str());
    }

    auto try_format_ability_scalar_needle_value(
            RC::Unreal::FProperty* property,
            const void* value_ptr,
            int element_size) -> std::optional<std::pair<std::wstring, std::wstring>>
    {
        if (!property || !value_ptr || element_size <= 0 || element_size > max_scalar_bytes)
        {
            return std::nullopt;
        }

        const std::wstring property_class = property->GetClass().GetName();
        if (property_class != STR("ByteProperty") &&
            property_class != STR("EnumProperty") &&
            property_class != STR("Int8Property") &&
            property_class != STR("UInt16Property") &&
            property_class != STR("Int16Property") &&
            property_class != STR("UInt32Property") &&
            property_class != STR("IntProperty") &&
            property_class != STR("UInt64Property") &&
            property_class != STR("Int64Property"))
        {
            return std::nullopt;
        }

        uint64_t raw_value = 0;
        std::memcpy(&raw_value, value_ptr, static_cast<size_t>(element_size));
        const std::wstring_view matched = try_get_ability_array_needle_name(raw_value);
        if (matched.empty())
        {
            return std::nullopt;
        }

        std::wostringstream value;
        value << STR("raw=") << raw_value
              << STR(" bytes=") << format_bytes_hex(value_ptr, static_cast<size_t>(element_size));
        return std::make_pair(std::wstring{matched}, value.str());
    }

    auto try_format_ability_map_needle_value(
            RC::Unreal::FProperty* property,
            const void* value_ptr) -> std::optional<std::pair<std::wstring, std::wstring>>
    {
        if (!property || !value_ptr || property->GetClass().GetName() != STR("MapProperty"))
        {
            return std::nullopt;
        }

        auto* map_property = static_cast<RC::Unreal::FMapProperty*>(property);
        RC::Unreal::FProperty* key_property = const_cast<RC::Unreal::FProperty*>(map_property->GetKeyProp());
        RC::Unreal::FProperty* value_property = const_cast<RC::Unreal::FProperty*>(map_property->GetValueProp());
        if (!key_property || !value_property)
        {
            return std::nullopt;
        }

        const int key_size = key_property->GetElementSize();
        const int value_size = value_property->GetElementSize();
        if (key_size <= 0 || value_size <= 0 || key_size > max_scalar_bytes || value_size > max_scalar_bytes)
        {
            return std::nullopt;
        }

        MapHeader header{};
        std::memcpy(&header, value_ptr, sizeof(MapHeader));
        if (!header.data_ptr || header.num <= 0 || header.num > max_scalar_map_entries || header.max < header.num)
        {
            return std::nullopt;
        }

        const int value_offset = static_cast<int>((static_cast<size_t>(key_size) + map_alignment_mask) & ~static_cast<size_t>(map_alignment_mask));
        const int raw_stride = value_offset + static_cast<int>((static_cast<size_t>(value_size) + map_alignment_mask) & ~static_cast<size_t>(map_alignment_mask)) + map_entry_metadata_bytes;
        if (raw_stride <= 0 || raw_stride > max_scalar_map_stride)
        {
            return std::nullopt;
        }

        const auto* base = reinterpret_cast<const std::byte*>(static_cast<uintptr_t>(header.data_ptr));
        std::set<std::wstring> matched_names{};
        std::wostringstream entries;
        const int entry_count = std::min(header.num, max_scalar_map_preview_entries);
        entries << STR("[");
        for (int index = 0; index < header.num; ++index)
        {
            const auto* entry = base + (static_cast<size_t>(index) * static_cast<size_t>(raw_stride));
            uint64_t key_raw = 0;
            uint64_t value_raw = 0;
            std::memcpy(&key_raw, entry, static_cast<size_t>(key_size));
            std::memcpy(&value_raw, entry + value_offset, static_cast<size_t>(value_size));
            if (std::wstring_view matched = try_get_ability_array_needle_name(key_raw); !matched.empty())
            {
                matched_names.emplace(matched);
            }
            if (std::wstring_view matched = try_get_ability_array_needle_name(value_raw); !matched.empty())
            {
                matched_names.emplace(matched);
            }

            if (index < entry_count)
            {
                if (index > 0)
                {
                    entries << STR(", ");
                }
                entries << STR("{k=") << key_raw << STR(",v=") << value_raw << STR(",raw=")
                        << format_bytes_hex(entry, static_cast<size_t>(std::min(raw_stride, scalar_map_entry_preview_bytes))) << STR("}");
            }
        }
        if (header.num > entry_count)
        {
            entries << STR(", ...");
        }
        entries << STR("]");

        if (matched_names.empty())
        {
            return std::nullopt;
        }

        std::wostringstream matches;
        bool first = true;
        for (const std::wstring& matched_name : matched_names)
        {
            if (!first)
            {
                matches << STR("|");
            }
            first = false;
            matches << matched_name;
        }

        std::wostringstream value;
        value << STR("key_class=") << key_property->GetClass().GetName()
              << STR(" key_size=") << key_size
              << STR(" value_class=") << value_property->GetClass().GetName()
              << STR(" value_size=") << value_size
              << STR(" num=") << header.num
              << STR(" max=") << header.max
              << STR(" stride=") << raw_stride
              << STR(" entries=") << entries.str();
        return std::make_pair(matches.str(), value.str());
    }
}
