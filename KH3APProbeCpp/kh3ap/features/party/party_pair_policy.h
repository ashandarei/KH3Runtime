#pragma once
#include <array>
#include <cstddef>

namespace kh3ap
{
    inline constexpr std::size_t runtime_party_member_slot_count = 2;
    inline constexpr std::size_t runtime_party_none_choice = 0;

    // KH3 compacts its friend slots. Preserve the user's two menu selections,
    // but use a dense pair for native calls, active state and slot verification.
    [[nodiscard]] constexpr auto
    runtime_party_physical_choices(std::array<std::size_t, runtime_party_member_slot_count> choices)
        -> std::array<std::size_t, runtime_party_member_slot_count>
    {
        if (choices[0] == runtime_party_none_choice)
        {
            return {choices[1], runtime_party_none_choice};
        }
        return choices;
    }
} // namespace kh3ap
