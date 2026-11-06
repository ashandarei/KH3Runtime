#pragma once
#include "kh3ap/bridge/serialization/json.h"
#include "kh3ap/bridge/serialization/json_fields.h"
#include "kh3ap/bridge/transport/bounded_read.h"
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <set>
#include <stdexcept>

namespace kh3ap {
    inline constexpr auto maximum_check_checkpoint_bytes = 1024 * 1024;
    inline constexpr std::size_t maximum_check_checkpoint_entries = 4096;

    // Read exactly once at configuration. Only absence creates an empty history;
    // syntax, type, scope and I/O errors must never replace an existing checkpoint.
    inline std::set<std::wstring> load_check_checkpoint(const std::filesystem::path& path,
                                                        std::wstring_view schema,
                                                        std::wstring_view seed, int64_t slot)
    {
        if (!std::filesystem::exists(path))
        {
            return {};
        }
        if (std::filesystem::file_size(path) > maximum_check_checkpoint_bytes)
        {
            throw std::runtime_error("check_checkpoint_size");
        }
        std::ifstream input(path, std::ios::binary);
        if (!input)
        {
            throw std::runtime_error("check_checkpoint_read");
        }
        const auto text = read_stream_bounded(input, maximum_check_checkpoint_bytes);
        if (!text)
        {
            throw std::runtime_error("check_checkpoint_read");
        }
        json::Value root;
        if (!json::parse(root, *text) || !root.is_object())
        {
            throw std::runtime_error("check_checkpoint_json");
        }
        const auto field = [&](const char* key) -> const json::Value&
        {
            const auto it = root.find(key);
            if (it == root.end())
            {
                throw std::runtime_error("check_checkpoint_field");
            }
            return *it;
        };
        const auto wide = [&](const json::Value& value) -> std::wstring
        {
            if (!value.is_string())
            {
                throw std::runtime_error("check_checkpoint_field");
            }
            auto decoded = json::utf8_to_wide(value.get_ref<const std::string&>());
            if (!decoded)
            {
                throw std::runtime_error("check_checkpoint_field");
            }
            return std::move(*decoded);
        };
        if (wide(field("schema")) != schema || wide(field("seed_name")) != seed)
        {
            throw std::runtime_error("check_checkpoint_scope");
        }
        const auto& number = field("slot");
        if (!number.is_number_integer() ||
            (number.is_number_unsigned() &&
             number.get<uint64_t>() > static_cast<uint64_t>(INT64_MAX)) ||
            number.get<int64_t>() != slot)
        {
            throw std::runtime_error("check_checkpoint_slot");
        }
        const auto& completed = field("completed");
        if (!completed.is_array())
        {
            throw std::runtime_error("check_checkpoint_field");
        }
        const auto& values = completed.get_ref<const json::Value::array_t&>();
        if (values.size() > maximum_check_checkpoint_entries)
        {
            throw std::runtime_error("check_checkpoint_count");
        }
        std::set<std::wstring> result;
        for (const auto& value : values)
        {
            if (!value.is_string())
            {
                throw std::runtime_error("check_checkpoint_id");
            }
            result.insert(wide(value));
        }
        return result;
    }
} // namespace kh3ap
