#pragma once
#include <chrono>
#include <string_view>

namespace kh3ap
{
    inline constexpr std::chrono::milliseconds color_apply_retry_interval{50};

    // Material updates do not require the item-grant system's 2.5-second
    // stability delay. All other refusals still block; map validity and
    // mesh readiness are checked before application.
    [[nodiscard]] inline auto runtime_color_safety_ready(
            bool grant_ready, std::wstring_view reason) -> bool
    {
        return grant_ready || reason == L"player_pawn_not_stable";
    }

    [[nodiscard]] inline auto runtime_color_map_allowed(std::wstring_view name) -> bool
    {
        return name.find(L":PersistentLevel") != std::wstring_view::npos &&
                name.find(L"Default__") == std::wstring_view::npos &&
                name.find(L"bt_51") == std::wstring_view::npos &&
                name.find(L"TresGameTitle") == std::wstring_view::npos &&
                name.find(L"StationOfAwakening") == std::wstring_view::npos;
    }
}
