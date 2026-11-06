#include "kh3ap/bridge/text_helpers.h"

#include "kh3ap/bridge/utilities.h"

#include <algorithm>

namespace kh3ap
{
    namespace
    {
        auto extract_path_root_after_prefix(
                std::wstring_view full_name,
                std::wstring_view prefix,
                size_t segment_count) -> std::optional<std::wstring>
        {
            const size_t begin = full_name.find(prefix);
            if (begin == std::wstring_view::npos)
            {
                return std::nullopt;
            }

            size_t end = begin + prefix.size();
            for (size_t index = 0; index < segment_count; ++index)
            {
                const size_t slash = full_name.find(L'/', end);
                if (slash == std::wstring_view::npos)
                {
                    return std::wstring{full_name.substr(begin)};
                }
                end = slash + 1;
            }

            if (end > begin && full_name[end - 1] == L'/')
            {
                --end;
            }
            return std::wstring{full_name.substr(begin, end - begin)};
        }
    }

    auto RuntimeTextHelpers::is_retry_source_label(std::wstring_view source_label) -> bool
    {
        return source_label.find(L" retry[") != std::wstring_view::npos;
    }

    auto RuntimeTextHelpers::normalize_vbonus_actor_reward_token(std::wstring_view value) -> std::wstring
    {
        std::wstring normalized{value};
        if (!normalized.starts_with(L"GIVESORA_"))
        {
            return normalized;
        }

        const size_t last_underscore = normalized.find_last_of(L'_');
        if (last_underscore == std::wstring::npos || last_underscore <= std::wstring_view{L"GIVESORA"}.size())
        {
            return normalized;
        }

        const std::wstring_view suffix{normalized.data() + last_underscore + 1, normalized.size() - last_underscore - 1};
        if (!suffix.empty() && std::all_of(suffix.begin(), suffix.end(), [](wchar_t ch) { return ch >= L'0' && ch <= L'9'; }))
        {
            normalized.resize(last_underscore);
        }

        return normalized;
    }

    auto RuntimeTextHelpers::contains_death_or_game_over_marker(std::wstring_view value) -> bool
    {
        const std::wstring upper = uppercase_ascii(value);
        return upper.find(L"DEAD") != std::wstring::npos ||
               upper.find(L"DEATH") != std::wstring::npos ||
               upper.find(L"DYING") != std::wstring::npos ||
               upper.find(L"GAMEOVER") != std::wstring::npos ||
               upper.find(L"GAME_OVER") != std::wstring::npos;
    }

    auto RuntimeTextHelpers::extract_level_path(std::wstring_view full_name) -> std::optional<std::wstring>
    {
        constexpr std::wstring_view level_prefix = L"/Game/Levels/";
        const size_t begin = full_name.find(level_prefix);
        if (begin == std::wstring_view::npos)
        {
            return std::nullopt;
        }

        const size_t end = full_name.find(L':', begin);
        if (end == std::wstring_view::npos || end <= begin)
        {
            return std::wstring{full_name.substr(begin)};
        }

        return std::wstring{full_name.substr(begin, end - begin)};
    }

    auto RuntimeTextHelpers::extract_game_map_root(std::wstring_view full_name) -> std::optional<std::wstring>
    {
        constexpr std::wstring_view level_prefix = L"/Game/Levels/";
        constexpr std::wstring_view maps_prefix = L"/Game/Maps/";
        if (auto level_root = extract_path_root_after_prefix(full_name, level_prefix, 2); level_root.has_value())
        {
            return level_root;
        }
        return extract_path_root_after_prefix(full_name, maps_prefix, 3);
    }
}
