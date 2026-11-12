#pragma once

#include <optional>
#include <string_view>

namespace kh3ap
{
    enum class RuntimeMenuFaceButton
    {
        none,
        confirm,
        back
    };

    constexpr auto is_menu_face_button(std::wstring_view key) -> bool
    {
        return key == L"Gamepad_FaceButton_Bottom" || key == L"Gamepad_FaceButton_Right";
    }

    constexpr auto menu_face_button(std::wstring_view key, std::optional<bool> exchange)
        -> RuntimeMenuFaceButton
    {
        if (!is_menu_face_button(key) || !exchange.has_value())
        {
            return RuntimeMenuFaceButton::none;
        }
        // TresLSIGameActor::NeedToExchangeActionButton returns true for
        // bottom/Cross = Decide, matching the existing GoA widget routing.
        return (key == L"Gamepad_FaceButton_Bottom") == *exchange ? RuntimeMenuFaceButton::confirm
                                                                  : RuntimeMenuFaceButton::back;
    }

    // Game-thread input boundary only. Read the current setting on demand;
    // keyboard/virtual keys never query the game or get swapped.
    auto runtime_menu_face_button(std::wstring_view key) -> RuntimeMenuFaceButton;
} // namespace kh3ap
