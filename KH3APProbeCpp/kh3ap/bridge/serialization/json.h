#pragma once

#include <cstdint>
#include <nlohmann/json.hpp>
#include "kh3ap/bridge/serialization/json_limits.h"
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace kh3ap::json
{
    inline constexpr int64_t max_safe_integer = 9007199254740991LL;

    using Value = nlohmann::json;

    // Complete documents only. Bounds apply before the decoder recurses; a
    // failed read replaces the output with null, never a partially parsed tree.
    auto parse(Value& output, std::string_view text) -> bool;
    auto write(const Value& value) -> std::string;
    inline auto write(const Value& value, std::string& output) -> void
    {
        output = write(value);
    }
    auto utf8_to_wide(std::string_view text) -> std::optional<std::wstring>;

    struct SeedDocument
    {
        Value value;
        std::string text;
    };
    using SeedDocumentSource = std::shared_ptr<const SeedDocument> (*)(const std::filesystem::path& path);
    // The game binds its installed pak source before constructing runtime services.
    auto bind_runtime_seed_source(SeedDocumentSource source) -> void;
    auto decode_seed_document(std::string_view name, std::string text)
        -> std::shared_ptr<const SeedDocument>;
    auto is_seed_document(const std::filesystem::path& path) -> bool;
    // An unavailable seed source or document fails the read.
    auto read_seed_document(const std::filesystem::path& path)
        -> std::shared_ptr<const SeedDocument>;
    inline auto read_seed_json(const std::filesystem::path& path) -> std::shared_ptr<const Value>
    {
        auto document = read_seed_document(path);
        return std::shared_ptr<const Value>(document, &document->value);
    }
} // namespace kh3ap::json
