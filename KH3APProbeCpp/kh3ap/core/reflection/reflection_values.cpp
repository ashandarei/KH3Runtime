#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/app/enums.h"
#include "kh3ap/bridge/utilities.h"

#include <UnrealDef.hpp>
#include <Unreal/FProperty.hpp>
#include <Unreal/FString.hpp>
#include <Unreal/NameTypes.hpp>
#include <Unreal/Property/FArrayProperty.hpp>
#include <Unreal/Property/FMapProperty.hpp>
#include <Unreal/Property/FStructProperty.hpp>
#include <Unreal/TMap.hpp>
#include <Unreal/UScriptStruct.hpp>
#include <Unreal/UClass.hpp>
#include <Unreal/UObject.hpp>
#include <algorithm>
#include <cstring>
#include <format>
#include <sstream>
#include <string>
#include <utility>
#include <vector>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace kh3ap
{
    namespace
    {
        constexpr auto max_inspected_string_array_entries = 256;
        constexpr auto max_inspected_object_array_entries = 128;

        constexpr uint32_t maximum_diagnostic_name_index = 500000;
        constexpr int maximum_diagnostic_element_size = 256;
        constexpr int maximum_diagnostic_hex_preview_bytes = 16;
        constexpr int maximum_array_diff_preview_bytes = 32;
    } // namespace

    auto decode_name_value(uint64_t raw_u64) -> DecodedNameValue
    {
        DecodedNameValue decoded{};
        decoded.low32 = static_cast<uint32_t>(raw_u64 & 0xffffffffull);
        decoded.high32 = static_cast<uint32_t>((raw_u64 >> 32) & 0xffffffffull);

        if (decoded.low32 == 0 || decoded.low32 >= maximum_diagnostic_name_index)
        {
            return decoded;
        }

        const RC::Unreal::FName first_name{decoded.low32, 0};
        decoded.fname_first = first_name.ToString();
#ifdef WITH_CASE_PRESERVING_NAME
        const RC::Unreal::FName pair_name{decoded.low32, decoded.high32, 0};
        decoded.fname_pair = pair_name.ToString();
#endif
        return decoded;
    }

    auto try_probe_pointer_bytes(uint64_t address, size_t size) -> std::optional<std::wstring>
    {
        if (address == 0 || size == 0)
        {
            return std::nullopt;
        }

        std::vector<unsigned char> buffer(size);
        SIZE_T bytes_read = 0;
        if (!ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<const void*>(address),
                               buffer.data(), buffer.size(), &bytes_read) ||
            bytes_read == 0)
        {
            return std::nullopt;
        }

        return format_bytes_hex(buffer.data(), bytes_read);
    }

    auto try_read_utf16_string(uint64_t address, size_t max_wchars) -> std::optional<std::wstring>
    {
        if (address == 0 || max_wchars == 0)
        {
            return std::nullopt;
        }

        std::vector<wchar_t> buffer(max_wchars + 1, L'\0');
        SIZE_T bytes_read = 0;
        const size_t bytes_to_read = max_wchars * sizeof(wchar_t);
        if (!ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<const void*>(address),
                               buffer.data(), bytes_to_read, &bytes_read) ||
            bytes_read < sizeof(wchar_t))
        {
            return std::nullopt;
        }

        const size_t wchar_count = bytes_read / sizeof(wchar_t);
        size_t end = 0;
        while (end < wchar_count && buffer[end] != L'\0')
        {
            ++end;
        }
        if (end == 0)
        {
            return std::nullopt;
        }

        const std::wstring value(buffer.data(), end);
        const bool printable =
            std::all_of(value.begin(), value.end(),
                        [](wchar_t ch)
                        {
                            return (ch >= L'0' && ch <= L'9') || (ch >= L'A' && ch <= L'Z') ||
                                   (ch >= L'a' && ch <= L'z') || ch == L'_' || ch == L'-';
                        });
        if (!printable)
        {
            return std::nullopt;
        }

        return value;
    }

    auto looks_like_process_pointer(uint64_t value) -> bool
    {
        // Wine/Proton can place valid 64-bit UE objects below the 4 GiB
        // boundary. Requiring a non-zero upper DWORD therefore rejects real
        // UObject pointers on Linux (for example, a Sora pawn at 0xb4bde8c0).
        // Use the address range reported by the Windows process and require a
        // committed, accessible mapping instead of assuming where its
        // allocator places objects.
        static const auto application_address_range = []
        {
            SYSTEM_INFO system_info{};
            GetSystemInfo(&system_info);
            return std::pair{reinterpret_cast<uintptr_t>(system_info.lpMinimumApplicationAddress),
                             reinterpret_cast<uintptr_t>(system_info.lpMaximumApplicationAddress)};
        }();

        const auto [minimum_address, maximum_address] = application_address_range;
        if (value < minimum_address || value > maximum_address)
        {
            return false;
        }

        MEMORY_BASIC_INFORMATION memory_info{};
        if (VirtualQuery(reinterpret_cast<const void*>(static_cast<uintptr_t>(value)), &memory_info,
                         sizeof(memory_info)) != sizeof(memory_info))
        {
            return false;
        }
        return memory_info.State == MEM_COMMIT &&
               (memory_info.Protect & (PAGE_GUARD | PAGE_NOACCESS)) == 0;
    }

    auto try_format_supported_map_entries(RC::Unreal::FProperty* property, const void* value_ptr)
        -> std::optional<std::wstring>
    {
        if (!property || !value_ptr)
        {
            return std::nullopt;
        }

        const auto* map_property = static_cast<RC::Unreal::FMapProperty*>(property);
        RC::Unreal::FProperty* key_property =
            const_cast<RC::Unreal::FProperty*>(map_property->GetKeyProp());
        RC::Unreal::FProperty* value_property =
            const_cast<RC::Unreal::FProperty*>(map_property->GetValueProp());
        if (!key_property || !value_property)
        {
            return std::nullopt;
        }

        const std::wstring key_class = key_property->GetClass().GetName();
        const std::wstring value_class = value_property->GetClass().GetName();
        if (key_class != STR("NameProperty") || value_class != STR("IntProperty"))
        {
            return std::nullopt;
        }

        MapHeader header{};
        std::memcpy(&header, value_ptr, sizeof(MapHeader));
        if (!header.data_ptr || header.num <= 0 || header.num > max_inspected_string_array_entries)
        {
            return std::nullopt;
        }

        using SupportedMap = RC::Unreal::TMap<uint64_t, int32_t>;
        SupportedMap supported_map(
            reinterpret_cast<SupportedMap::MapElement*>(static_cast<uintptr_t>(header.data_ptr)),
            header.num, header.max);

        std::wostringstream stream;
        stream << STR("[");
        const int entry_count = std::min(header.num, 16);
        auto* entries = supported_map.GetElementsPtr();
        for (int index = 0; index < entry_count; ++index)
        {
            if (index > 0)
            {
                stream << STR(", ");
            }

            const auto decoded = decode_name_value(entries[index].Key);
            const std::wstring& key_name =
                !decoded.fname_pair.empty() && decoded.fname_pair != STR("None")
                    ? decoded.fname_pair
                    : decoded.fname_first;
            stream << STR("{key=")
                   << (!key_name.empty() ? key_name
                                         : std::format(STR("raw_{}"), entries[index].Key))
                   << STR(", value=") << entries[index].Value << STR("}");
        }
        if (header.num > entry_count)
        {
            stream << STR(", ...");
        }
        stream << STR("]");
        return stream.str();
    }

    auto try_collect_supported_array_entries(RC::Unreal::FProperty* property, const void* value_ptr)
        -> std::optional<std::vector<std::wstring>>
    {
        if (!property || !value_ptr)
        {
            return std::nullopt;
        }

        const auto* array_property = static_cast<RC::Unreal::FArrayProperty*>(property);
        RC::Unreal::FProperty* inner_property =
            const_cast<RC::Unreal::FProperty*>(array_property->GetInner());
        if (!inner_property)
        {
            return std::nullopt;
        }

        ArrayHeader header{};
        std::memcpy(&header, value_ptr, sizeof(ArrayHeader));
        if (!header.data_ptr || header.num <= 0 || header.num > max_inspected_object_array_entries)
        {
            return std::nullopt;
        }

        const std::wstring inner_class = inner_property->GetClass().GetName();
        const int inner_size = inner_property->GetElementSize();
        if (inner_size <= 0 || inner_size > maximum_diagnostic_element_size)
        {
            return std::nullopt;
        }

        std::vector<std::wstring> entries_out{};

        if (inner_class == STR("StructProperty"))
        {
            auto* struct_property = static_cast<RC::Unreal::FStructProperty*>(inner_property);
            RC::Unreal::UScriptStruct* script_struct = struct_property->GetStruct();
            const std::wstring struct_name =
                script_struct ? script_struct->GetName() : STR("<unknown>");
            if (struct_name.find(STR("TresEventStructure")) == std::wstring::npos)
            {
                return std::nullopt;
            }

            const int entry_count = std::min(header.num, 8);
            const auto* base =
                reinterpret_cast<const std::byte*>(static_cast<uintptr_t>(header.data_ptr));
            entries_out.reserve(entry_count);
            for (int index = 0; index < entry_count; ++index)
            {
                const void* entry_ptr =
                    base + (static_cast<size_t>(index) * static_cast<size_t>(inner_size));
                std::wostringstream entry_stream;
                entry_stream << STR("{idx=") << index << STR(", raw=")
                             << format_bytes_hex(
                                    entry_ptr, static_cast<size_t>(std::min(
                                                   inner_size, maximum_array_diff_preview_bytes)));

                const int qword_count = std::min(inner_size / 8, 7);
                if (qword_count > 0)
                {
                    entry_stream << STR(", parts=[");
                    for (int qword_index = 0; qword_index < qword_count; ++qword_index)
                    {
                        if (qword_index > 0)
                        {
                            entry_stream << STR(", ");
                        }

                        uint64_t qword_value = 0;
                        std::memcpy(&qword_value,
                                    static_cast<const std::byte*>(entry_ptr) + (qword_index * 8),
                                    sizeof(uint64_t));
                        const auto decoded = decode_name_value(qword_value);
                        const std::wstring& candidate =
                            !decoded.fname_pair.empty() && decoded.fname_pair != STR("None")
                                ? decoded.fname_pair
                                : decoded.fname_first;
                        entry_stream << qword_index << STR(":");
                        if (!candidate.empty())
                        {
                            entry_stream << candidate;
                        }
                        else
                        {
                            entry_stream << qword_value;
                        }
                    }
                    entry_stream << STR("]");
                }

                entry_stream << STR("}");
                entries_out.emplace_back(entry_stream.str());
            }
        }
        else if (inner_class == STR("NameProperty"))
        {
            const int entry_count = std::min(header.num, 16);
            const auto* base =
                reinterpret_cast<const std::byte*>(static_cast<uintptr_t>(header.data_ptr));
            entries_out.reserve(entry_count);
            for (int index = 0; index < entry_count; ++index)
            {
                uint64_t raw_name = 0;
                std::memcpy(&raw_name,
                            base + (static_cast<size_t>(index) * static_cast<size_t>(inner_size)),
                            sizeof(uint64_t));
                const auto decoded = decode_name_value(raw_name);
                const std::wstring& candidate =
                    !decoded.fname_pair.empty() && decoded.fname_pair != STR("None")
                        ? decoded.fname_pair
                        : decoded.fname_first;
                entries_out.emplace_back(candidate.empty() ? std::format(STR("raw_{}"), raw_name)
                                                           : candidate);
            }
        }
        else
        {
            return std::nullopt;
        }

        return entries_out;
    }

    auto try_collect_array_entry_diff_signatures(RC::Unreal::FProperty* property,
                                                 const void* value_ptr, size_t max_entries)
        -> std::optional<std::vector<std::wstring>>
    {
        if (!property || !value_ptr || max_entries == 0)
        {
            return std::nullopt;
        }

        const auto* array_property = static_cast<RC::Unreal::FArrayProperty*>(property);
        RC::Unreal::FProperty* inner_property =
            const_cast<RC::Unreal::FProperty*>(array_property->GetInner());
        if (!inner_property)
        {
            return std::nullopt;
        }

        ArrayHeader header{};
        std::memcpy(&header, value_ptr, sizeof(ArrayHeader));
        if (header.num < 0)
        {
            return std::nullopt;
        }

        constexpr int32_t absolute_max_num = 4096;
        if (header.num > absolute_max_num)
        {
            return std::nullopt;
        }

        if (header.num == 0)
        {
            return std::vector<std::wstring>{};
        }

        if (!header.data_ptr)
        {
            return std::nullopt;
        }

        const std::wstring inner_class = inner_property->GetClass().GetName();
        const int inner_size = inner_property->GetElementSize();
        if (inner_size <= 0 || inner_size > maximum_diagnostic_element_size)
        {
            return std::nullopt;
        }

        const int entry_count = std::min(header.num, static_cast<int32_t>(max_entries));
        std::vector<std::wstring> signatures_out{};
        signatures_out.reserve(entry_count);

        if (inner_class == STR("StructProperty"))
        {
            const auto* base =
                reinterpret_cast<const std::byte*>(static_cast<uintptr_t>(header.data_ptr));
            for (int index = 0; index < entry_count; ++index)
            {
                const void* entry_ptr =
                    base + (static_cast<size_t>(index) * static_cast<size_t>(inner_size));
                signatures_out.emplace_back(format_bytes_hex(
                    entry_ptr,
                    static_cast<size_t>(std::min(inner_size, maximum_array_diff_preview_bytes))));
            }
        }
        else if (inner_class == STR("NameProperty"))
        {
            const auto* base =
                reinterpret_cast<const std::byte*>(static_cast<uintptr_t>(header.data_ptr));
            for (int index = 0; index < entry_count; ++index)
            {
                uint64_t raw_name = 0;
                std::memcpy(&raw_name,
                            base + (static_cast<size_t>(index) * static_cast<size_t>(inner_size)),
                            sizeof(uint64_t));
                const auto decoded = decode_name_value(raw_name);
                const std::wstring& candidate =
                    !decoded.fname_pair.empty() && decoded.fname_pair != STR("None")
                        ? decoded.fname_pair
                        : decoded.fname_first;
                signatures_out.emplace_back(candidate.empty() ? std::format(STR("raw_{}"), raw_name)
                                                              : candidate);
            }
        }
        else
        {
            return std::nullopt;
        }

        return signatures_out;
    }

    auto try_format_supported_array_entries(RC::Unreal::FProperty* property, const void* value_ptr)
        -> std::optional<std::wstring>
    {
        const auto entries = try_collect_supported_array_entries(property, value_ptr);
        if (!entries.has_value())
        {
            return std::nullopt;
        }

        std::wostringstream stream;
        stream << STR("entries=[");
        for (size_t index = 0; index < entries->size(); ++index)
        {
            if (index > 0)
            {
                stream << STR(", ");
            }
            stream << (*entries)[index];
        }
        stream << STR("]");
        return stream.str();
    }

    auto format_hook_param_value(RC::Unreal::FProperty* property, const void* value_ptr,
                                 int element_size) -> std::wstring
    {
        if (!property || !value_ptr || element_size <= 0)
        {
            return STR("<none>");
        }

        const std::wstring property_class = property->GetClass().GetName();
        if (property_class == STR("NameProperty"))
        {
            if (element_size >= static_cast<int>(sizeof(RC::Unreal::FName)))
            {
                const auto* const name_value = static_cast<const RC::Unreal::FName*>(value_ptr);
                const std::wstring direct_name =
                    name_value ? name_value->ToString() : std::wstring{};
                if (!direct_name.empty() && direct_name != STR("None"))
                {
                    return direct_name;
                }
            }
            const uint64_t raw_u64 = read_unsigned_param_value(value_ptr, element_size);
            const auto decoded = decode_name_value(raw_u64);
            if (!decoded.fname_pair.empty() && decoded.fname_pair != STR("None"))
            {
                return decoded.fname_pair;
            }
            if (!decoded.fname_first.empty() && decoded.fname_first != STR("None"))
            {
                return decoded.fname_first;
            }
            return std::format(STR("raw_name=0x{:X}"), raw_u64);
        }
        if (property_class == STR("BoolProperty"))
        {
            return read_unsigned_param_value(value_ptr, std::min(element_size, 1)) != 0
                       ? STR("true")
                       : STR("false");
        }
        if (property_class == STR("ObjectProperty"))
        {
            RC::Unreal::UObject* const object_value =
                *static_cast<RC::Unreal::UObject* const*>(const_cast<void*>(value_ptr));
            return format_object_debug_name(object_value);
        }
        if (property_class == STR("EnumProperty") || property_class == STR("ByteProperty") ||
            property_class == STR("IntProperty") || property_class == STR("Int64Property") ||
            property_class == STR("UInt32Property") || property_class == STR("UInt64Property"))
        {
            return std::to_wstring(read_unsigned_param_value(value_ptr, element_size));
        }
        return format_bytes_hex(
            value_ptr,
            static_cast<size_t>(std::clamp(element_size, 1, maximum_diagnostic_hex_preview_bytes)));
    }

    auto format_stable_ability_trace_value(RC::Unreal::FProperty* property, const void* value_ptr,
                                           int element_size, std::wstring_view param_name)
        -> std::wstring
    {
        if (!property || !value_ptr || element_size <= 0)
        {
            return STR("<none>");
        }

        const std::wstring property_class = property->GetClass().GetName();
        if (property_class == STR("BoolProperty"))
        {
            return read_unsigned_param_value(value_ptr, std::min(element_size, 1)) != 0
                       ? STR("true")
                       : STR("false");
        }
        if (property_class == STR("NameProperty"))
        {
            const uint64_t raw_u64 = read_unsigned_param_value(value_ptr, element_size);
            const auto decoded = decode_name_value(raw_u64);
            if (!decoded.fname_pair.empty() && decoded.fname_pair != STR("None"))
            {
                return decoded.fname_pair;
            }
            if (!decoded.fname_first.empty() && decoded.fname_first != STR("None"))
            {
                return decoded.fname_first;
            }
            return std::format(STR("raw_name=0x{:X}"), raw_u64);
        }
        if (property_class == STR("ObjectProperty"))
        {
            return std::format(STR("ptr=0x{:X}"),
                               read_unsigned_param_value(value_ptr, element_size));
        }
        if (property_class == STR("EnumProperty") || property_class == STR("ByteProperty") ||
            property_class == STR("IntProperty") || property_class == STR("Int64Property") ||
            property_class == STR("UInt32Property") || property_class == STR("UInt64Property"))
        {
            const uint64_t raw_value = read_unsigned_param_value(value_ptr, element_size);
            if (param_name == STR("InAbilityKind"))
            {
                const std::wstring ability_name = try_format_runtime_ability_enum_value(raw_value);
                if (!ability_name.empty())
                {
                    return std::format(STR("{} ({})"), raw_value, ability_name);
                }
            }
            return std::to_wstring(raw_value);
        }
        return format_bytes_hex(
            value_ptr,
            static_cast<size_t>(std::clamp(element_size, 1, maximum_diagnostic_hex_preview_bytes)));
    }
} // namespace kh3ap
