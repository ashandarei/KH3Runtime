#pragma once
#include <algorithm>
#include <string>
#include <string_view>

namespace kh3ap
{
    inline auto kh02_format_challenge_text(std::wstring value, int number, int target) -> std::wstring
    {
        constexpr std::wstring_view placeholder = L"{0}";
        constexpr std::wstring_view line_break = L"<br>";
        constexpr std::wstring_view aqua_name = L"Aqua";
        constexpr int first_player_named_challenge = 7;
        constexpr int last_player_named_challenge = 9;
        const auto count = std::to_wstring(target);
        for (size_t at = 0; (at = value.find(placeholder, at)) != std::wstring::npos; at += count.size())
        {
            value.replace(at, placeholder.size(), count);
        }
        for (size_t at = 0; (at = value.find(line_break, at)) != std::wstring::npos; ++at)
        {
            value.replace(at, line_break.size(), L"\n");
        }
        for (size_t at = 0; (at = value.find(L'<', at)) != std::wstring::npos;)
        {
            auto end = value.find(L'>', at);
            if (end == std::wstring::npos)
            {
                break;
            }
            value.erase(at, end - at + 1);
        }
        if (number >= first_player_named_challenge && number <= last_player_named_challenge)
        {
            const auto aqua = value.find(aqua_name);
            if (aqua != std::wstring::npos)
            {
                value.replace(aqua, aqua_name.size(), L"the current player");
            }
        }

        return value;
    }

    inline auto kh02_format_challenge_progress(bool active, int count, int target) -> std::wstring
    {
        if (!active || target <= 0)
        {
            return {};
        }
        return std::to_wstring(std::min(count, target)) + L" / " + std::to_wstring(target);
    }
} // namespace kh3ap
