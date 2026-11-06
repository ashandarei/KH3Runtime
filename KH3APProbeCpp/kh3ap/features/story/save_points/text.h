#pragma once
#include <cstddef>
#include <optional>
#include <iosfwd>
#include <string>
#include <string_view>
#include <vector>
namespace kh3ap::save_point_text
{

    auto append_json_string_field(std::ostringstream& json, std::string_view key,
                                  std::wstring_view value) -> void;

    auto append_json_number_field(std::ostringstream& json, std::string_view key, double value)
        -> void;

    auto append_json_optional_bool_field(std::ostringstream& json, std::string_view key,
                                         const std::optional<bool>& value) -> void;

    auto string_field_or(const std::string& text, std::string_view primary_key,
                         std::string_view fallback_key, std::wstring_view default_value)
        -> std::wstring;

    auto parent_path_text(std::wstring_view path) -> std::wstring;

    auto runtime_context_matches_map(std::wstring_view context_text, std::wstring_view map_path)
        -> bool;

    auto auto_spawn_context_key(std::wstring_view map_path) -> std::wstring;

    auto sanitize_name_token(std::wstring_view raw, size_t max_chars) -> std::wstring;

    auto bool_text(bool value) -> std::wstring;

    auto object_lookup_candidates(std::wstring_view raw_path) -> std::vector<std::wstring>;
} // namespace kh3ap::save_point_text
