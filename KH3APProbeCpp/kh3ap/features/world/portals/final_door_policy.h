#pragma once
#include <array>
#include <string_view>

namespace kh3ap::final_door
{
    inline constexpr int minimum_required_hearts = 3;
    inline constexpr int maximum_required_hearts = 30;
    inline constexpr int maximum_required_worlds = 7;

    struct WorldFlag { std::wstring_view name; int threshold; bool boolean; };
    inline constexpr std::array<WorldFlag, 9> worlds{{
        {L"gameflow_HE", 9999, false}, {L"gameflow_TT", 520, false},
        {L"gameflow_TS", 9999, false}, {L"gameflow_RA", 9999, false},
        {L"gameflow_MI", 9999, false}, {L"gameflow_FZ", 8820, false},
        {L"gameflow_CA", 9999, false}, {L"gameflow_BX", 9999, false},
        {L"gameflow_VBONUS_067_dw_001", 1, true},
    }};
    constexpr bool enabled(bool ready, int hearts, int required_hearts, int completed, int required_worlds)
    {
        return ready && required_hearts >= minimum_required_hearts &&
               required_hearts <= maximum_required_hearts && required_worlds >= 0 &&
               required_worlds <= maximum_required_worlds && hearts >= required_hearts &&
               completed >= required_worlds;
    }
}
