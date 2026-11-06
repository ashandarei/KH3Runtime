#pragma once

#include <cstdint>
#include <string_view>

namespace kh3ap
{
    inline constexpr std::uint32_t message_box_button_type_mask = 0x0fU;
    inline constexpr std::uint32_t message_box_yes_no_buttons = 0x04U;

    // Match only the observed English title/gameplay exit prompts. The low four bits are the
    // Win32 message-box button type; icon/default-button flags may vary.
    inline auto is_exit_confirmation_dialog(const wchar_t* text, const wchar_t* caption,
                                            std::uint32_t type) -> bool
    {
        if (!text || !caption ||
            (type & message_box_button_type_mask) != message_box_yes_no_buttons ||
            std::wstring_view{caption} != L"KINGDOM HEARTS III")
        {
            return false;
        }
        // EGS pads each line. Gameplay adds a second line warning about
        // unsaved progress; this is still the normal confirmed-exit path.
        const auto trim_end = [](std::wstring_view value)
        {
            const auto last = value.find_last_not_of(L" \t\r\n");
            return last == std::wstring_view::npos ? std::wstring_view{}
                                                   : value.substr(0, last + 1);
        };
        const auto message = trim_end(std::wstring_view{text});
        const auto newline = message.find(L'\n');
        if (newline == std::wstring_view::npos)
        {
            return message == L"Exit the game?";
        }
        return trim_end(message.substr(0, newline)) == L"Exit the game?" &&
               message.substr(newline + 1) == L"All unsaved game progress will be lost.";
    }
} // namespace kh3ap
