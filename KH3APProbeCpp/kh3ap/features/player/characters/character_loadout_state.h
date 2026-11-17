#pragma once

#include <array>
#include <cstdint>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <string>
#include <nlohmann/json.hpp>

namespace kh3ap::character_loadouts
{
    // The native assignment copies this prefix as scalar values only. The
    // later ability/progression fields are deliberately outside this snapshot.
    inline constexpr size_t equipment_bytes = 0x158;
    inline constexpr size_t record_bytes = 0x9c0;
    inline constexpr size_t supplemental_offset = 0x9572ac;
    inline constexpr std::array<std::array<uint8_t, 2>, 14> identities{{
        {6, 10}, {3, 14}, {4, 14}, {5, 14}, {7, 14}, {8, 14}, {9, 14},
        {36, 14}, {37, 14}, {1, 11}, {1, 12}, {3, 16}, {7, 17}, {39, 14}}};
    using Record = std::array<uint8_t, equipment_bytes>;
    using Snapshot = std::array<Record, identities.size()>;
    using SnapshotPtr = std::shared_ptr<const Snapshot>;

    inline void validate(const Snapshot& snapshot)
    {
        for (size_t i = 0; i < snapshot.size(); ++i)
        {
            const auto& record = snapshot[i];
            if (record[0] != identities[i][0] || record[1] != identities[i][1] ||
                record[2] > 3 || record[3] > 8 || record[4] > 8 || record[5] > 8)
            {
                throw std::runtime_error("character_loadout_record_invalid");
            }
        }
    }

    inline auto capture(const std::byte* save_data) -> SnapshotPtr
    {
        auto result = std::make_shared<Snapshot>();
        for (size_t i = 0; i < result->size(); ++i)
        {
            std::memcpy((*result)[i].data(), save_data + supplemental_offset + i * record_bytes,
                        equipment_bytes);
        }
        validate(*result);
        return result;
    }

    inline void restore(std::byte* save_data, const Snapshot& snapshot)
    {
        validate(snapshot);
        // Validate every destination before changing any of them. Empty slots
        // are data too: never merge a loaded save with the outgoing session.
        for (size_t i = 0; i < snapshot.size(); ++i)
        {
            const auto* record = save_data + supplemental_offset + i * record_bytes;
            if (std::to_integer<uint8_t>(record[0]) != identities[i][0] ||
                std::to_integer<uint8_t>(record[1]) != identities[i][1])
            {
                throw std::runtime_error("character_loadout_destination_invalid");
            }
        }
        for (size_t i = 0; i < snapshot.size(); ++i)
        {
            std::memcpy(save_data + supplemental_offset + i * record_bytes,
                        snapshot[i].data(), equipment_bytes);
        }
    }

    inline auto to_json(const SnapshotPtr& snapshot) -> nlohmann::json
    {
        if (!snapshot) { return nullptr; }
        validate(*snapshot);
        auto records = nlohmann::json::array();
        constexpr char hex[] = "0123456789abcdef";
        for (const auto& record : *snapshot)
        {
            std::string bytes;
            bytes.reserve(equipment_bytes * 2);
            for (const auto value : record)
            {
                bytes.push_back(hex[value >> 4]);
                bytes.push_back(hex[value & 15]);
            }
            records.push_back(std::move(bytes));
        }
        return {{"version", 1}, {"records", std::move(records)}};
    }

    inline auto from_json(const nlohmann::json& json) -> SnapshotPtr
    {
        if (json.is_null()) { return {}; }
        if (!json.is_object() || json.value("version", 0) != 1 ||
            !json.contains("records") || !json["records"].is_array() ||
            json["records"].size() != identities.size())
        {
            throw std::runtime_error("character_loadout_schema_invalid");
        }
        auto result = std::make_shared<Snapshot>();
        const auto nibble = [](char c) -> uint8_t
        {
            if (c >= '0' && c <= '9') { return c - '0'; }
            if (c >= 'a' && c <= 'f') { return c - 'a' + 10; }
            throw std::runtime_error("character_loadout_hex_invalid");
        };
        for (size_t i = 0; i < result->size(); ++i)
        {
            const auto& value = json["records"][i];
            if (!value.is_string() || value.get_ref<const std::string&>().size() != equipment_bytes * 2)
            {
                throw std::runtime_error("character_loadout_size_invalid");
            }
            const auto& bytes = value.get_ref<const std::string&>();
            for (size_t j = 0; j < equipment_bytes; ++j)
            {
                (*result)[i][j] = (nibble(bytes[j * 2]) << 4) | nibble(bytes[j * 2 + 1]);
            }
        }
        validate(*result);
        return result;
    }
}
