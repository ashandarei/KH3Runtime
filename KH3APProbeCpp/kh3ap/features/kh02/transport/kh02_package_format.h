#pragma once
#include "kh02_package_adapter.h"
#include <algorithm>
#include <array>
#include <bit>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <string>
#include <type_traits>

namespace kh02asset::package
{
    constexpr std::size_t import_stride = 28;
    constexpr std::size_t export_stride = 104;
    constexpr std::size_t maximum_header_bytes = 16 * 1024 * 1024;
    constexpr std::size_t maximum_export_bytes = 128 * 1024 * 1024;

    inline void require(bool valid, const char* message)
    {
        if (!valid)
        {
            throw std::runtime_error(message);
        }
    }

    inline void range(std::span<const uint8_t> bytes, std::size_t offset, std::size_t size)
    {
        require(offset <= bytes.size() && size <= bytes.size() - offset, "kh02_package_bounds");
    }

    template <typename T> auto get(std::span<const uint8_t> bytes, std::size_t offset) -> T
    {
        static_assert(std::is_trivially_copyable_v<T> &&
                      std::endian::native == std::endian::little);
        range(bytes, offset, sizeof(T));
        T result;
        std::memcpy(&result, bytes.data() + offset, sizeof(T));
        return result;
    }

    template <typename T> void put(Bytes& bytes, std::size_t offset, T value)
    {
        range(bytes, offset, sizeof(T));
        std::memcpy(bytes.data() + offset, &value, sizeof(T));
    }

    template <typename T> void append(Bytes& bytes, T value)
    {
        const auto offset = bytes.size();
        bytes.resize(offset + sizeof(T));
        put(bytes, offset, value);
    }

    struct Name
    {
        std::string ascii;
        std::size_t start{}, end{};
        bool wide{};
        uint16_t insensitive{}, sensitive{};
    };

    struct Import
    {
        int32_t class_package{}, kind{}, outer{}, name{}, number{};
        std::string path;
    };

    struct Header
    {
        std::vector<Name> names;
        std::vector<Import> imports;
        std::size_t name_start{}, name_end{}, import_start{}, export_start{}, export_count{};

        auto name(int32_t index) const -> const std::string&
        {
            require(index >= 0 && static_cast<std::size_t>(index) < names.size(),
                    "kh02_package_name_index");
            return names[index].ascii;
        }

        auto name_index(std::string_view value) const -> int32_t
        {
            const auto found = std::find_if(names.begin(), names.end(),
                                            [&](const Name& entry)
                                            {
                                                return entry.ascii == value;
                                            });
            require(found != names.end(), "kh02_package_required_name");
            return static_cast<int32_t>(found - names.begin());
        }

        auto import_index(std::string_view value) const -> int32_t
        {
            const auto found = std::find_if(imports.begin(), imports.end(),
                                            [&](const Import& entry)
                                            {
                                                return entry.path == value;
                                            });
            require(found != imports.end(), "kh02_package_required_import");
            return -1 - static_cast<int32_t>(found - imports.begin());
        }

        auto has_import(std::string_view value) const -> bool
        {
            return std::any_of(imports.begin(), imports.end(),
                               [&](const Import& entry)
                               {
                                   return entry.path == value;
                               });
        }
    };

    inline auto read_name(std::span<const uint8_t> bytes, std::size_t& cursor, bool hashes) -> Name
    {
        Name result;
        result.start = cursor;
        const auto count = get<int32_t>(bytes, cursor);
        require(count != 0 && count > -100'000 && count < 100'000, "kh02_package_string_count");
        result.wide = count < 0;
        const auto units = static_cast<std::size_t>(count < 0 ? -count : count);
        const auto width = result.wide ? 2U : 1U;
        cursor += sizeof(int32_t);
        range(bytes, cursor, units * width);
        bool ascii = true;
        for (std::size_t i = 0; i < units; ++i)
        {
            const auto character =
                result.wide ? get<uint16_t>(bytes, cursor + i * width) : bytes[cursor + i];
            if (i + 1 == units)
            {
                require(character == 0, "kh02_package_string_terminator");
            }
            else
            {
                require(character != 0, "kh02_package_embedded_null");
                ascii &= character < 128;
                result.ascii.push_back(static_cast<char>(character));
            }
        }
        if (!ascii)
        {
            // Non-ASCII names are retained byte for byte. Compatibility
            // identities and package paths use ASCII in these verified builds.
            result.ascii.clear();
        }
        cursor += units * width;
        if (hashes)
        {
            result.insensitive = get<uint16_t>(bytes, cursor);
            result.sensitive = get<uint16_t>(bytes, cursor + 2);
            cursor += 4;
        }
        result.end = cursor;
        return result;
    }

    inline auto parse(std::span<const uint8_t> bytes) -> Header
    {
        require(bytes.size() <= maximum_header_bytes, "kh02_package_header_limit");
        require(get<uint32_t>(bytes, 0) == 0x9e2a83c1 && get<int32_t>(bytes, 4) == -7,
                "kh02_package_summary");
        const auto custom = get<int32_t>(bytes, 20);
        require(custom >= 0 && custom < 1000, "kh02_package_custom_versions");
        std::size_t cursor = 28 + static_cast<std::size_t>(custom) * 20;
        read_name(bytes, cursor, false);
        const auto nonnegative = [&](std::size_t at, int32_t limit)
        {
            const auto result = get<int32_t>(bytes, at);
            require(result >= 0 && result < limit, "kh02_package_table_value");
            return static_cast<std::size_t>(result);
        };
        const auto name_count = nonnegative(cursor + 4, 1'000'000);
        Header result;
        result.name_start = nonnegative(cursor + 8, std::numeric_limits<int32_t>::max());
        result.export_count = nonnegative(cursor + 20, 100'000);
        result.export_start = nonnegative(cursor + 24, std::numeric_limits<int32_t>::max());
        const auto import_count = nonnegative(cursor + 28, 100'000);
        result.import_start = nonnegative(cursor + 32, std::numeric_limits<int32_t>::max());
        require(name_count && result.name_start >= cursor + 36 &&
                    result.import_start + import_count * import_stride == result.export_start,
                "kh02_package_table_layout");
        range(bytes, result.import_start, import_count * import_stride);
        range(bytes, result.export_start, result.export_count * export_stride);
        cursor = result.name_start;
        for (std::size_t i = 0; i < name_count; ++i)
        {
            result.names.push_back(read_name(bytes, cursor, true));
        }
        result.name_end = cursor;
        require(cursor <= result.import_start, "kh02_package_name_table_overlap");
        for (std::size_t i = 0; i < import_count; ++i)
        {
            const auto at = result.import_start + i * import_stride;
            Import entry{get<int32_t>(bytes, at),      get<int32_t>(bytes, at + 8),
                         get<int32_t>(bytes, at + 16), get<int32_t>(bytes, at + 20),
                         get<int32_t>(bytes, at + 24), {}};
            result.name(entry.class_package);
            result.name(entry.kind);
            result.name(entry.name);
            require(entry.number >= 0, "kh02_package_name_number");
            require(entry.outer >= -static_cast<int32_t>(import_count) &&
                        entry.outer <= static_cast<int32_t>(result.export_count),
                    "kh02_package_import_outer");
            result.imports.push_back(std::move(entry));
        }
        for (std::size_t i = 0; i < import_count; ++i)
        {
            auto index = -1 - static_cast<int32_t>(i);
            std::vector<std::string> parts;
            for (std::size_t depth = 0; index < 0; ++depth)
            {
                require(depth < import_count && depth < 128, "kh02_package_import_cycle");
                const auto& entry = result.imports[-index - 1];
                auto value = result.name(entry.name);
                if (entry.number)
                {
                    value += "_" + std::to_string(entry.number - 1);
                }
                parts.push_back(std::move(value));
                index = entry.outer;
            }
            for (auto part = parts.rbegin(); part != parts.rend(); ++part)
            {
                if (!result.imports[i].path.empty())
                {
                    result.imports[i].path += '.';
                }
                result.imports[i].path += *part;
            }
        }
        return result;
    }

    inline auto name_hashes(std::string_view value, bool wide) -> std::array<uint16_t, 2>
    {
        static const auto table = []
        {
            std::array<uint32_t, 256> result{};
            for (uint32_t i = 0; i < result.size(); ++i)
            {
                auto crc = i << 24;
                for (int bit = 0; bit < 8; ++bit)
                {
                    crc = (crc << 1) ^ ((crc & 0x80000000) ? 0x04c11db7U : 0U);
                }
                result[i] = crc;
            }
            return result;
        }();
        uint32_t insensitive{}, sensitive = 0xffffffffU;
        const auto crc_byte = [&](uint8_t byte)
        {
            sensitive ^= byte;
            for (int bit = 0; bit < 8; ++bit)
            {
                sensitive = (sensitive >> 1) ^ ((sensitive & 1) ? 0xedb88320U : 0U);
            }
        };
        for (const unsigned char character : value)
        {
            require(character < 128, "kh02_package_changed_name_nonascii");
            const auto upper =
                character >= 'a' && character <= 'z' ? character - ('a' - 'A') : character;
            insensitive = (insensitive >> 8) ^ table[(insensitive ^ upper) & 255];
            if (wide)
            {
                insensitive = (insensitive >> 8) ^ table[insensitive & 255];
            }
            crc_byte(character);
            crc_byte(0);
            crc_byte(0);
            crc_byte(0);
        }
        return {static_cast<uint16_t>(insensitive), static_cast<uint16_t>(sensitive ^ 0xffffffffU)};
    }

    inline auto occurrences(std::span<const uint8_t> bytes, std::span<const uint8_t> needle)
        -> std::vector<std::size_t>
    {
        require(!needle.empty(), "kh02_package_empty_pattern");
        std::vector<std::size_t> result;
        auto cursor = bytes.begin();
        while (cursor != bytes.end())
        {
            const auto found = std::search(cursor, bytes.end(), needle.begin(), needle.end());
            if (found == bytes.end())
            {
                break;
            }
            result.push_back(static_cast<std::size_t>(found - bytes.begin()));
            cursor = found + needle.size();
        }
        return result;
    }

    inline auto property_tag(const Header& header, std::string_view name, std::string_view type,
                             int32_t size) -> Bytes
    {
        Bytes result;
        for (const auto value : {header.name_index(name), 0, header.name_index(type), 0, size, 0})
        {
            append(result, value);
        }
        append<uint8_t>(result, 0);
        return result;
    }
} // namespace kh02asset::package
