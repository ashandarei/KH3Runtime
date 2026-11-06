#include "kh3ap/core/settings/pak_seed_settings.h"
#include "kh3ap/bridge/serialization/json_limits.h"
#include <Windows.h>
#include <bcrypt.h>
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <regex>
#include <span>
#include <stdexcept>
#include <vector>

namespace kh3ap
{
    namespace
    {
        constexpr int32_t maximum_pak_string_characters = 32768;
        constexpr uint32_t pak_magic = 0x5a6f12e1;
        constexpr uint32_t supported_pak_version = 4;
        constexpr size_t sha1_digest_bytes = 20;
        constexpr uint64_t minimum_index_entry_bytes = 62;
        constexpr size_t compressed_block_record_bytes = 16;
        constexpr uint16_t ascii_character_limit = 128;

        constexpr uint64_t max_index_bytes = 64 * 1024 * 1024;
        constexpr uint64_t max_settings_bytes = 1024 * 1024;
        constexpr std::string_view settings_name = "KINGDOM HEARTS III/Content/Archipelago/runtime_seed_settings.json";
        using Digest = std::array<unsigned char, sha1_digest_bytes>;

        Digest sha1(std::span<const char> bytes)
        {
            Digest result{};
            if (BCryptHash(BCRYPT_SHA1_ALG_HANDLE, nullptr, 0,
                           reinterpret_cast<PUCHAR>(const_cast<char*>(bytes.data())), static_cast<ULONG>(bytes.size()),
                           result.data(), static_cast<ULONG>(result.size())) < 0)
                throw std::runtime_error("Seed pak SHA-1 failed");
            return result;
        }

        struct Cursor
        {
            std::span<const char> bytes;
            size_t position{};
            template<class T> T get()
            {
                if (sizeof(T) > bytes.size() - position) throw std::runtime_error("Truncated seed pak index");
                T value;
                std::memcpy(&value, bytes.data() + position, sizeof(value));
                position += sizeof(value);
                return value;
            }
            std::string string()
            {
                const int32_t length = get<int32_t>();
                if (!length || length < -maximum_pak_string_characters ||
                    length > maximum_pak_string_characters)
                {
                    throw std::runtime_error("Invalid seed pak string");
                }
                std::string result;
                const int count = length < 0 ? -length : length;
                for (int i = 0; i < count; ++i)
                {
                    const auto value = length < 0 ? get<uint16_t>() : get<uint8_t>();
                    if (i == count - 1)
                    {
                        if (value) throw std::runtime_error("Unterminated seed pak string");
                    }
                    else
                    {
                        if (!value) throw std::runtime_error("Embedded null in seed pak path");
                        // Only the ASCII settings path and mount point are matched.
                        result.push_back(value < ascii_character_limit ? static_cast<char>(value)
                                                                       : '?');
                    }
                }
                return result;
            }
        };

        std::vector<char> read(std::ifstream& stream, uint64_t offset, size_t count)
        {
            stream.seekg(static_cast<std::streamoff>(offset));
            std::vector<char> bytes(count);
            if (!stream.read(bytes.data(), static_cast<std::streamsize>(count))) throw std::runtime_error("Could not read seed pak");
            return bytes;
        }
    }

    static auto read_records(const std::filesystem::path& pak, bool include_catalog) -> std::map<std::string, std::string>
    {
        std::ifstream stream(pak, std::ios::binary | std::ios::ate);
        if (!stream) throw std::runtime_error("Could not open installed seed pak");
        const auto size = stream.tellg();
        constexpr size_t footer_size = 45;
        if (size < static_cast<std::streamoff>(footer_size)) throw std::runtime_error("Truncated seed pak footer");
        const auto footer_bytes = read(stream, static_cast<uint64_t>(size) - footer_size, footer_size);
        Cursor footer{footer_bytes};
        if (footer.get<uint8_t>() != 0 || footer.get<uint32_t>() != pak_magic ||
            footer.get<uint32_t>() != supported_pak_version)
        {
            throw std::runtime_error("Expected unencrypted V4 seed pak");
        }
        const auto index_offset = footer.get<uint64_t>();
        const auto index_size = footer.get<uint64_t>();
        const auto index_hash = footer.get<Digest>();
        if (index_size > max_index_bytes || index_offset > static_cast<uint64_t>(size) - footer_size ||
            index_size != static_cast<uint64_t>(size) - footer_size - index_offset)
            throw std::runtime_error("Invalid seed pak index bounds");
        const auto index_bytes = read(stream, index_offset, static_cast<size_t>(index_size));
        if (sha1(index_bytes) != index_hash) throw std::runtime_error("Seed pak index checksum mismatch");
        Cursor index{index_bytes};
        if (index.string() != "../../../") throw std::runtime_error("Unexpected seed pak mount point");
        const auto count = index.get<uint32_t>();
        if (count > index_size / minimum_index_entry_bytes)
        {
            throw std::runtime_error("Invalid seed pak entry count");
        }
        std::map<std::string, std::string> records;
        for (uint32_t i = 0; i < count; ++i)
        {
            const auto name = index.string();
            const auto entry_start = index.position;
            const auto offset = index.get<uint64_t>();
            const auto stored_size = index.get<uint64_t>();
            const auto raw_size = index.get<uint64_t>();
            const auto codec = index.get<uint32_t>();
            const auto digest = index.get<Digest>();
            if (codec > 1) throw std::runtime_error("Unsupported seed pak codec");
            if (codec)
            {
                const auto blocks = index.get<uint32_t>();
                if (!blocks ||
                    blocks > (index.bytes.size() - index.position) / compressed_block_record_bytes)
                {
                    throw std::runtime_error("Invalid seed pak block count");
                }
                for (uint32_t block = 0; block < blocks; ++block)
                {
                    (void)index.get<uint64_t>();
                    (void)index.get<uint64_t>();
                }
            }
            if (index.get<uint8_t>() != 0) throw std::runtime_error("Encrypted seed pak entry");
            (void)index.get<uint32_t>();
            const auto header_size = index.position - entry_start;
            if (offset > index_offset || header_size > index_offset - offset || stored_size > index_offset - offset - header_size)
                throw std::runtime_error("Seed pak entry overlaps index");
            constexpr std::string_view prefix = "KINGDOM HEARTS III/Content/Archipelago/";
            if (!name.starts_with(prefix)) continue;
            const auto filename = name.substr(prefix.size());
            const bool settings = name == settings_name;
            const bool catalog = include_catalog && filename == "slot_data.json";
            const bool hints = include_catalog && filename == "runtime_seed_hints.json";
            if (!settings && !catalog && !hints) continue;
            try
            {
                const auto limit = catalog ? json::limits::seed_bytes : max_settings_bytes;
                if (records.contains(filename) || codec || stored_size != raw_size || raw_size == 0 || raw_size > limit)
                    throw std::runtime_error("Invalid stored seed settings record; regenerate the seed pak");
                auto header = read(stream, offset, header_size);
                Cursor local{header};
                if (local.get<uint64_t>() != 0) throw std::runtime_error("Invalid seed settings local offset");
                if (!std::equal(header.begin() + 8, header.end(), index_bytes.begin() + entry_start + 8))
                    throw std::runtime_error("Seed settings header mismatch");
                const auto payload = read(stream, offset + header_size, static_cast<size_t>(raw_size));
                if (sha1(payload) != digest) throw std::runtime_error("Seed settings checksum mismatch");
                records.emplace(filename, std::string(payload.begin(), payload.end()));
            }
            catch (const std::exception&)
            {
                if (!hints) throw;
                records.erase(filename);
            }
        }
        if (index.position != index.bytes.size()) throw std::runtime_error("Trailing seed pak index data");
        if (!records.contains("runtime_seed_settings.json") || (include_catalog && !records.contains("slot_data.json")))
            throw std::runtime_error("Seed pak has no settings/catalog; regenerate the seed pak");
        return records;
    }

    auto read_pak_seed_settings(const std::filesystem::path& pak) -> std::string
    {
        return read_records(pak, false).at("runtime_seed_settings.json");
    }

    auto read_pak_seed_records(const std::filesystem::path& pak) -> std::map<std::string, std::string>
    {
        return read_records(pak, true);
    }

    auto find_seed_pak(const std::filesystem::path& mods) -> std::filesystem::path
    {
        const std::wregex pattern{LR"(^pakchunk9-.+-randomizer-AP_[0-9]+-[0-9]+_99_P\.pak$)", std::regex::icase};
        std::optional<std::filesystem::path> result;
        for (const auto& entry : std::filesystem::recursive_directory_iterator(mods))
        {
            if (!entry.is_regular_file() || !std::regex_match(entry.path().filename().wstring(), pattern)) continue;
            if (result) throw std::runtime_error("Multiple KH3 seed paks installed; select one run before launching");
            result = entry.path();
        }
        if (!result) throw std::runtime_error("No KH3 seed pak installed");
        return *result;
    }
}
