#include "kh3ap/diagnostics/ability_probe/limits.h"
#include "kh3ap/diagnostics/ability_probe/ability_reflection_probe_helpers.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/bridge/utilities.h"
#include <Unreal/UObject.hpp>
#include <Unreal/UClass.hpp>
#include <Unreal/FProperty.hpp>
#include <Unreal/Property/FArrayProperty.hpp>
#include <Unreal/Property/FMapProperty.hpp>
#include <Unreal/Property/FStructProperty.hpp>
#include <Unreal/UScriptStruct.hpp>
#include <algorithm>
#include <cstring>
#include <format>
#include <sstream>

namespace kh3ap
{
    using namespace ability_probe_limits;
    namespace
    {
        struct NestedArrayFieldProbe
        {
            std::wstring name;
            int offset{};
            int inner_size{1};
        };

        auto collect_nested_array_field_probes(RC::Unreal::FProperty* value_property) -> std::vector<NestedArrayFieldProbe>
        {
            std::vector<NestedArrayFieldProbe> fields{};
            if (!value_property)
            {
                return fields;
            }

            const std::wstring value_class = value_property->GetClass().GetName();
            if (value_class == STR("ArrayProperty"))
            {
                auto* array_property = static_cast<RC::Unreal::FArrayProperty*>(value_property);
                RC::Unreal::FProperty* inner_property = const_cast<RC::Unreal::FProperty*>(array_property->GetInner());
                fields.push_back({STR("<value_array>"), 0, inner_property ? std::clamp(inner_property->GetElementSize(), 1, max_scalar_bytes) : 1});
                return fields;
            }

            if (value_class == STR("StructProperty"))
            {
                auto* struct_property = static_cast<RC::Unreal::FStructProperty*>(value_property);
                if (RC::Unreal::UScriptStruct* script_struct = struct_property->GetStruct())
                {
                    for (RC::Unreal::FProperty* nested_property : script_struct->ForEachPropertyInChain())
                    {
                        if (!nested_property || nested_property->GetClass().GetName() != STR("ArrayProperty"))
                        {
                            continue;
                        }

                        auto* nested_array_property = static_cast<RC::Unreal::FArrayProperty*>(nested_property);
                        RC::Unreal::FProperty* inner_property = const_cast<RC::Unreal::FProperty*>(nested_array_property->GetInner());
                        fields.push_back(
                                {nested_property->GetName(), nested_property->GetOffset_Internal(), inner_property ? std::clamp(inner_property->GetElementSize(), 1, max_scalar_bytes) : 1});
                        if (fields.size() >= max_nested_array_fields)
                        {
                            break;
                        }
                    }
                }
            }

            if (fields.empty() && value_property->GetElementSize() >= static_cast<int>(sizeof(ArrayHeader)))
            {
                fields.push_back({STR("<value_as_u8_array>"), 0, 1});
            }
            return fields;
        }

        auto format_manual_nested_array_entry_value(const void* entry_ptr, int element_size) -> std::wstring
        {
            if (!entry_ptr || element_size <= 0 || element_size > max_scalar_bytes)
            {
                return STR("<invalid>");
            }

            uint64_t raw_value = 0;
            std::memcpy(&raw_value, entry_ptr, static_cast<size_t>(element_size));

            std::wostringstream stream;
            stream << raw_value;
            if (std::wstring_view matched = try_get_ability_array_needle_name(raw_value); !matched.empty())
            {
                stream << STR(":") << matched;
            }

            if (element_size == static_cast<int>(sizeof(RC::Unreal::UObject*)) && raw_value != 0)
            {
                RC::Unreal::UObject* object_value = nullptr;
                std::memcpy(&object_value, entry_ptr, sizeof(RC::Unreal::UObject*));
                if (kh3ap::is_uobject_valid_fast(object_value))
                {
                    RC::Unreal::UClass* object_class = object_value->GetClassPrivate();
                    stream << STR(":object=") << object_value->GetFullName()
                           << STR(":class=") << (object_class ? object_class->GetName() : std::wstring{STR("<null>")});
                }
                else
                {
                    stream << STR(":object_real=false");
                }
            }

            return stream.str();
        }

        auto format_manual_nested_enum_array(const ArrayHeader& header, int element_size) -> std::wstring
        {
            std::wostringstream stream;
            stream << STR("num=") << header.num
                   << STR(" max=") << header.max
                   << STR(" ptr=0x") << std::hex << header.data_ptr << std::dec
                   << STR(" elem=") << element_size;

            if (!header.data_ptr || header.num <= 0 || header.num > max_nested_enum_entries || header.max < header.num || element_size <= 0 || element_size > max_scalar_bytes)
            {
                return stream.str();
            }

            const auto* base = reinterpret_cast<const std::byte*>(static_cast<uintptr_t>(header.data_ptr));
            const int entry_count = std::min(header.num, max_nested_enum_preview_entries);
            stream << STR(" entries=[");
            for (int index = 0; index < entry_count; ++index)
            {
                if (index > 0)
                {
                    stream << STR(", ");
                }

                const auto* entry_ptr = base + (static_cast<size_t>(index) * static_cast<size_t>(element_size));
                stream << format_manual_nested_array_entry_value(entry_ptr, element_size);
            }
            if (header.num > entry_count)
            {
                stream << STR(", ...");
            }
            stream << STR("]");

            const int byte_count = std::min(header.num * element_size, nested_enum_preview_bytes);
            stream << STR(" bytes=") << format_bytes_hex(base, static_cast<size_t>(byte_count));
            return stream.str();
        }
    }

    auto format_manual_object_pointer_value(const void* value_ptr) -> std::wstring
    {
        if (!value_ptr)
        {
            return STR("<null_value_ptr>");
        }

        RC::Unreal::UObject* object_value = nullptr;
        std::memcpy(&object_value, value_ptr, sizeof(RC::Unreal::UObject*));
        if (!object_value)
        {
            return STR("ptr=0x0");
        }
        if (kh3ap::is_uobject_valid_fast(object_value))
        {
            return format_object_debug_name(object_value);
        }
        return std::format(STR("ptr=0x{:X} real=false"), reinterpret_cast<uintptr_t>(object_value));
    }

    auto read_manual_object_pointer(const void* value_ptr) -> RC::Unreal::UObject*
    {
        if (!value_ptr)
        {
            return nullptr;
        }

        RC::Unreal::UObject* object_value = nullptr;
        std::memcpy(&object_value, value_ptr, sizeof(RC::Unreal::UObject*));
        if (!object_value || !kh3ap::is_uobject_valid_fast(object_value))
        {
            return nullptr;
        }
        return object_value;
    }

    auto format_manual_array_at_offset(
            const std::byte* object_bytes,
            size_t offset,
            int assumed_element_size) -> std::wstring
    {
        if (!object_bytes)
        {
            return STR("<null_object>");
        }

        ArrayHeader header{};
        std::memcpy(&header, object_bytes + offset, sizeof(ArrayHeader));
        std::wostringstream stream;
        stream << STR("num=") << header.num
               << STR(" max=") << header.max
               << STR(" ptr=0x") << std::hex << header.data_ptr << std::dec
               << STR(" assumed_element_size=") << assumed_element_size;

        if (header.data_ptr && header.num > 0 && header.num <= max_manual_array_entries && assumed_element_size > 0 && assumed_element_size <= max_manual_array_element_bytes)
        {
            const auto* base = reinterpret_cast<const std::byte*>(static_cast<uintptr_t>(header.data_ptr));
            const int entry_count = std::min(header.num, max_manual_preview_entries);
            stream << STR(" entries=[");
            for (int index = 0; index < entry_count; ++index)
            {
                if (index > 0)
                {
                    stream << STR(", ");
                }

                const void* entry_ptr = base + (static_cast<size_t>(index) * static_cast<size_t>(assumed_element_size));
                if (assumed_element_size == static_cast<int>(sizeof(RC::Unreal::UObject*)))
                {
                    stream << format_manual_object_pointer_value(entry_ptr);
                }
                else
                {
                    stream << format_bytes_hex(entry_ptr, static_cast<size_t>(std::min(assumed_element_size, manual_array_entry_preview_bytes)));
                }
            }
            if (header.num > entry_count)
            {
                stream << STR(", ...");
            }
            stream << STR("]");
        }

        return stream.str();
    }

    auto format_manual_map_at_property(
            RC::Unreal::UObject* object,
            std::wstring_view property_name,
            size_t raw_entry_size) -> std::wstring
    {
        if (!object || !kh3ap::is_uobject_valid_fast(object))
        {
            return STR("<null_object>");
        }

        RC::Unreal::FProperty* property = object->GetPropertyByNameInChain(std::wstring{property_name}.c_str());
        void* value_ptr = object->GetValuePtrByPropertyNameInChain(std::wstring{property_name}.c_str());
        if (!property || !value_ptr || property->GetClass().GetName() != STR("MapProperty"))
        {
            return STR("<missing_map_property>");
        }

        auto* map_property = static_cast<RC::Unreal::FMapProperty*>(property);
        RC::Unreal::FProperty* key_property = const_cast<RC::Unreal::FProperty*>(map_property->GetKeyProp());
        RC::Unreal::FProperty* value_property = const_cast<RC::Unreal::FProperty*>(map_property->GetValueProp());

        MapHeader header{};
        std::memcpy(&header, value_ptr, sizeof(MapHeader));
        std::wostringstream stream;
        stream << STR("key_class=") << (key_property ? key_property->GetClass().GetName() : std::wstring{STR("<none>")})
               << STR(" key_size=") << (key_property ? key_property->GetElementSize() : 0)
               << STR(" value_class=") << (value_property ? value_property->GetClass().GetName() : std::wstring{STR("<none>")})
               << STR(" value_size=") << (value_property ? value_property->GetElementSize() : 0)
               << STR(" num=") << header.num
               << STR(" max=") << header.max
               << STR(" ptr=0x") << std::hex << header.data_ptr << std::dec
               << STR(" raw_entry_size=") << raw_entry_size;

        if (header.data_ptr && header.num > 0 && header.num <= max_manual_map_entries && raw_entry_size > 0 && raw_entry_size <= max_manual_map_entry_bytes)
        {
            const auto* base = reinterpret_cast<const std::byte*>(static_cast<uintptr_t>(header.data_ptr));
            const int entry_count = std::min(header.num, max_manual_preview_entries);
            stream << STR(" raw_entries=[");
            for (int index = 0; index < entry_count; ++index)
            {
                if (index > 0)
                {
                    stream << STR(", ");
                }
                stream << format_bytes_hex(base + (static_cast<size_t>(index) * raw_entry_size), std::min(raw_entry_size, static_cast<size_t>(manual_map_entry_preview_bytes)));
            }
            if (header.num > entry_count)
            {
                stream << STR(", ...");
            }
            stream << STR("]");
        }

        return stream.str();
    }

    auto format_manual_map_nested_arrays_at_property(
            RC::Unreal::UObject* object,
            std::wstring_view property_name) -> std::wstring
    {
        if (!object || !kh3ap::is_uobject_valid_fast(object))
        {
            return STR("<null_object>");
        }

        RC::Unreal::FProperty* property = object->GetPropertyByNameInChain(std::wstring{property_name}.c_str());
        void* value_ptr = object->GetValuePtrByPropertyNameInChain(std::wstring{property_name}.c_str());
        if (!property || !value_ptr || property->GetClass().GetName() != STR("MapProperty"))
        {
            return STR("<missing_map_property>");
        }

        auto* map_property = static_cast<RC::Unreal::FMapProperty*>(property);
        RC::Unreal::FProperty* key_property = const_cast<RC::Unreal::FProperty*>(map_property->GetKeyProp());
        RC::Unreal::FProperty* value_property = const_cast<RC::Unreal::FProperty*>(map_property->GetValueProp());
        const int key_size = key_property ? key_property->GetElementSize() : 0;
        const int value_size = value_property ? value_property->GetElementSize() : 0;
        if (key_size <= 0 || key_size > max_scalar_bytes || value_size <= 0 || value_size > max_nested_map_value_bytes)
        {
            return STR("<unsupported_map_layout>");
        }

        MapHeader header{};
        std::memcpy(&header, value_ptr, sizeof(MapHeader));
        const int value_offset = static_cast<int>((static_cast<size_t>(key_size) + map_alignment_mask) & ~static_cast<size_t>(map_alignment_mask));
        const int raw_stride = value_offset + static_cast<int>((static_cast<size_t>(value_size) + map_alignment_mask) & ~static_cast<size_t>(map_alignment_mask)) + map_entry_metadata_bytes;
        const std::vector<NestedArrayFieldProbe> fields = collect_nested_array_field_probes(value_property);

        std::wostringstream stream;
        stream << STR("key_class=") << (key_property ? key_property->GetClass().GetName() : std::wstring{STR("<none>")})
               << STR(" key_size=") << key_size
               << STR(" value_class=") << (value_property ? value_property->GetClass().GetName() : std::wstring{STR("<none>")})
               << STR(" value_size=") << value_size
               << STR(" num=") << header.num
               << STR(" max=") << header.max
               << STR(" ptr=0x") << std::hex << header.data_ptr << std::dec
               << STR(" stride=") << raw_stride
               << STR(" nested_fields=[");
        for (size_t index = 0; index < fields.size(); ++index)
        {
            if (index > 0)
            {
                stream << STR(", ");
            }
            stream << fields[index].name << STR("@") << fields[index].offset << STR(":") << fields[index].inner_size;
        }
        stream << STR("]");

        if (!header.data_ptr || header.num <= 0 || header.num > max_manual_map_entries || header.max < header.num || raw_stride <= 0 || raw_stride > max_nested_map_stride || fields.empty())
        {
            return stream.str();
        }

        const auto* base = reinterpret_cast<const std::byte*>(static_cast<uintptr_t>(header.data_ptr));
        const int entry_count = std::min(header.num, max_nested_map_preview_entries);
        stream << STR(" decoded_entries=[");
        for (int index = 0; index < entry_count; ++index)
        {
            if (index > 0)
            {
                stream << STR(", ");
            }

            const auto* entry = base + (static_cast<size_t>(index) * static_cast<size_t>(raw_stride));
            uint64_t key_raw = 0;
            std::memcpy(&key_raw, entry, static_cast<size_t>(key_size));
            const auto* value_base = entry + value_offset;
            stream << STR("{key=") << key_raw;

            for (const NestedArrayFieldProbe& field : fields)
            {
                if (field.offset < 0 || field.offset + static_cast<int>(sizeof(ArrayHeader)) > value_size)
                {
                    stream << STR(", ") << field.name << STR("=<offset_out_of_range>");
                    continue;
                }

                ArrayHeader nested_header{};
                std::memcpy(&nested_header, value_base + field.offset, sizeof(ArrayHeader));
                stream << STR(", ") << field.name << STR("={")
                       << format_manual_nested_enum_array(nested_header, field.inner_size)
                       << STR("}");
            }
            stream << STR("}");
        }
        if (header.num > entry_count)
        {
            stream << STR(", ...");
        }
        stream << STR("]");
        return stream.str();
    }
}
