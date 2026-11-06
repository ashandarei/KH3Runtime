#include "kh3ap/features/story/save_points/text.h"
#include "kh3ap/bridge/serialization/json_fields.h"
#include "kh3ap/bridge/utilities.h"
#include <algorithm>
#include <iomanip>
#include <sstream>
#include <utility>

namespace kh3ap::save_point_text
{
    namespace
    {
        constexpr int coordinate_decimal_places = 3;
    }
    auto append_json_string_field(std::ostringstream& json, std::string_view key,
                                  std::wstring_view value) -> void
    {
        json << ",\"" << key << "\":\"" << json_escape(value) << "\"";
    }

    auto append_json_number_field(std::ostringstream& json, std::string_view key, double value)
        -> void
    {
        json << ",\"" << key << "\":" << std::fixed << std::setprecision(coordinate_decimal_places)
             << value;
    }

    auto append_json_optional_bool_field(std::ostringstream& json, std::string_view key,
                                         const std::optional<bool>& value) -> void
    {
        json << ",\"" << key << "\":";
        if (value.has_value())
        {
            json << (*value ? "true" : "false");
        }
        else
        {
            json << "null";
        }
    }

    auto string_field_or(const std::string& text, std::string_view primary_key,
                         std::string_view fallback_key, std::wstring_view default_value)
        -> std::wstring
    {
        if (const auto value = JsonFieldReader::string_field(text, primary_key); value.has_value())
        {
            return *value;
        }
        if (const auto value = JsonFieldReader::string_field(text, fallback_key); value.has_value())
        {
            return *value;
        }
        return std::wstring{default_value};
    }

    auto parent_path_text(std::wstring_view path) -> std::wstring
    {
        const size_t last_slash = path.rfind(L'/');
        if (last_slash == std::wstring_view::npos || last_slash == 0)
        {
            return std::wstring{path};
        }
        return std::wstring{path.substr(0, last_slash)};
    }

    auto runtime_context_matches_map(std::wstring_view context_text, std::wstring_view map_path)
        -> bool
    {
        if (map_path.empty())
        {
            return false;
        }
        if (context_text.find(map_path) != std::wstring_view::npos)
        {
            return true;
        }

        const std::wstring map_root = parent_path_text(map_path);
        if (map_root.empty())
        {
            return false;
        }
        const std::wstring map_root_prefix = map_root + L"/";
        return context_text.find(map_root_prefix) != std::wstring_view::npos;
    }

    auto auto_spawn_context_key(std::wstring_view map_path) -> std::wstring
    {
        // The lifecycle owner resets this state on a real player/map
        // start. A pawn can be replaced while the same streamed map visit
        // remains active, so its address must not rearm actor spawning.
        return parent_path_text(map_path);
    }

    auto sanitize_name_token(std::wstring_view raw, size_t max_chars) -> std::wstring
    {
        std::wstring result{};
        result.reserve(std::min(raw.size(), max_chars));
        for (wchar_t ch : raw)
        {
            if (result.size() >= max_chars)
            {
                break;
            }
            const bool is_ascii_alnum = (ch >= L'a' && ch <= L'z') || (ch >= L'A' && ch <= L'Z') ||
                                        (ch >= L'0' && ch <= L'9');
            result.push_back(is_ascii_alnum ? ch : L'_');
        }
        while (!result.empty() && result.back() == L'_')
        {
            result.pop_back();
        }
        return result;
    }

    auto bool_text(bool value) -> std::wstring
    {
        return value ? std::wstring{L"true"} : std::wstring{L"false"};
    }

    auto object_lookup_candidates(std::wstring_view raw_path) -> std::vector<std::wstring>
    {
        std::vector<std::wstring> candidates{};
        if (raw_path.empty())
        {
            return candidates;
        }

        const auto append_unique = [&candidates](std::wstring value)
        {
            if (value.empty())
            {
                return;
            }
            if (std::find(candidates.begin(), candidates.end(), value) == candidates.end())
            {
                candidates.push_back(std::move(value));
            }
        };

        append_unique(std::wstring{raw_path});

        const size_t first_quote = raw_path.find(L'\'');
        if (first_quote != std::wstring_view::npos)
        {
            const size_t second_quote = raw_path.find(L'\'', first_quote + 1);
            if (second_quote != std::wstring_view::npos && second_quote > first_quote + 1)
            {
                append_unique(
                    std::wstring{raw_path.substr(first_quote + 1, second_quote - first_quote - 1)});
            }
        }

        return candidates;
    }
} // namespace kh3ap::save_point_text
