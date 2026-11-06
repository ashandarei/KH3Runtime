#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>
#include <string_view>
#include <utility>

namespace kh3ap::keyblade_options
{
    inline constexpr std::wstring_view s_runtime_token_prefix = L"KH3AP_KEYBLADE_STYLE::";
    inline constexpr std::wstring_view s_keyblade_name_prefix = L"WEP_KEYBLADE";

    inline auto is_valid_keyblade_name(std::wstring_view name) -> bool
    {
        return name.starts_with(s_keyblade_name_prefix) && name.size() == 14 && name[12] >= L'0' &&
               name[12] <= L'9' && name[13] >= L'0' && name[13] <= L'9';
    }

    inline auto keyblade_style_ability_value(std::wstring_view name) -> std::optional<uint8_t>
    {
        constexpr std::array<std::pair<std::wstring_view, uint8_t>, 17> styles{{
            {L"WEP_KEYBLADE00", 117},
            {L"WEP_KEYBLADE01", 118},
            {L"WEP_KEYBLADE02", 119},
            {L"WEP_KEYBLADE03", 120},
            {L"WEP_KEYBLADE04", 121},
            {L"WEP_KEYBLADE05", 122},
            {L"WEP_KEYBLADE06", 123},
            {L"WEP_KEYBLADE07", 124},
            {L"WEP_KEYBLADE08", 125},
            {L"WEP_KEYBLADE09", 126},
            {L"WEP_KEYBLADE10", 127},
            {L"WEP_KEYBLADE11", 128},
            {L"WEP_KEYBLADE12", 129},
            {L"WEP_KEYBLADE13", 130},
            {L"WEP_KEYBLADE14", 131},
            {L"WEP_KEYBLADE17", 132},
            {L"WEP_KEYBLADE18", 133},
        }};
        const auto match = std::find_if(styles.begin(), styles.end(),
                                        [name](const auto& style)
                                        {
                                            return style.first == name;
                                        });
        return match == styles.end() ? std::nullopt : std::optional<uint8_t>{match->second};
    }

} // namespace kh3ap::keyblade_options
