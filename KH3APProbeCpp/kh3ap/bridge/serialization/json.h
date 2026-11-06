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
    // Restore the immutable bundled catalog plus seed-specific overrides.
    auto expand_slot_data(Value compact) -> Value;

    struct SeedDocument
    {
        Value value;
        std::string text;
    };
    using SeedDocumentSource = std::shared_ptr<const SeedDocument> (*)(std::string_view name);
    // The game binds its pak source before constructing runtime services.
    // Standalone tools may still read files, but a bound runtime never falls
    // back to loose files, including when the pak source reports an error.
    auto bind_runtime_seed_source(SeedDocumentSource source) -> void;
    auto decode_seed_document(std::string_view name, std::string text)
        -> std::shared_ptr<const SeedDocument>;
    auto is_seed_document(const std::filesystem::path& path) -> bool;
    // Shared immutable documents; replaced files invalidate the cached entry.
    // Read/parse/size errors propagate with the source filename.
    auto read_seed_document(const std::filesystem::path& path)
        -> std::shared_ptr<const SeedDocument>;
    inline auto read_seed_json(const std::filesystem::path& path) -> std::shared_ptr<const Value>
    {
        auto document = read_seed_document(path);
        return std::shared_ptr<const Value>(document, &document->value);
    }
} // namespace kh3ap::json
