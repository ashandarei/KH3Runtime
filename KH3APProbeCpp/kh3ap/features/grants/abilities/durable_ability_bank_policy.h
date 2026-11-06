#pragma once

#include <cstdint>

namespace kh3ap
{
    // Each of the three ability slots has independent equipped/new flags.
    // Camp updates the active bank before the transition bank catches up.
    // Ownership, grant sources, counts and every other bit must still agree.
    inline constexpr uint32_t runtime_ability_menu_flags = 0x0666;

    constexpr auto runtime_durable_ability_banks_match(uint32_t active, uint32_t staging) -> bool
    {
        return ((active ^ staging) & ~runtime_ability_menu_flags) == 0;
    }
}
