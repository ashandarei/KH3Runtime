#pragma once
#include "save_slot_policy.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <optional>
#include <string>
#include <string_view>

namespace kh3ap
{
    // EGS native catalog ABI. These are already-loaded metadata, never save files.
    inline constexpr size_t native_catalog_entry_stride = 0xf88;
    inline constexpr size_t native_catalog_slot_offset = 0x60;
    inline constexpr size_t native_catalog_map_offset = 0x88;
    inline constexpr size_t native_catalog_map_capacity = 0x100;
    inline constexpr int32_t native_catalog_maximum_entries = save_slots::catalog_capacity;

    template <class Character> constexpr bool kh02_map_package(std::basic_string_view<Character> package)
    {
        for (const std::string_view prefix :
             {"/Game/Maps/APKH02CastleTown/", "/Game/Maps/APKH02WorldWithin/", "/Game/Maps/APKH02Mirrors/"})
        {
            if (package.size() <= prefix.size())
            {
                continue;
            }
            bool matches = true;
            for (size_t index = 0; index < prefix.size(); ++index)
            {
                matches = matches && package[index] == static_cast<Character>(prefix[index]);
            }
            if (matches)
            {
                return true;
            }
        }
        return false;
    }

    template <class Read>
    auto native_save_destination(uintptr_t entries, int32_t count, int32_t requested_slot, Read read)
        -> std::optional<std::string>
    {
        if (!entries || count < 1 || count > native_catalog_maximum_entries ||
            !save_slots::is_native_slot(requested_slot))
        {
            return std::nullopt;
        }
        for (int32_t index = 0; index < count; ++index)
        {
            const auto entry = entries + static_cast<uintptr_t>(index) * native_catalog_entry_stride;
            uint8_t slot{};
            if (!read(entry + native_catalog_slot_offset, slot))
            {
                return std::nullopt;
            }
            if (slot != requested_slot)
            {
                continue;
            }
            std::array<char, native_catalog_map_capacity> buffer{};
            if (!read(entry + native_catalog_map_offset, buffer))
            {
                return std::nullopt;
            }
            size_t length = 0;
            while (length < buffer.size() && buffer[length])
            {
                const auto character = buffer[length];
                if (!((character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z') ||
                      (character >= '0' && character <= '9') || character == '_' || character == '/'))
                {
                    return std::nullopt;
                }
                ++length;
            }
            const std::string_view package(buffer.data(), length);
            if (length == buffer.size() || !package.starts_with("/Game/") || package.size() <= 6 ||
                package.ends_with('/') || package.find("//") != std::string_view::npos)
            {
                return std::nullopt;
            }
            return std::string(package);
        }
        return std::nullopt;
    }
} // namespace kh3ap
