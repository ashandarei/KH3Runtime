#pragma once

#include <cstdint>

namespace kh3ap::save_slots
{
    inline constexpr int autosave = 0;
    inline constexpr int first_manual = 1;
    inline constexpr int last_manual = 99;
    inline constexpr int first_agent_test = 50;

    // The native menu lists manual slots separately from its autosave widget.
    inline constexpr int manual_count = last_manual - first_manual + 1;
    inline constexpr int last_manual_list_index = manual_count - 1;
    inline constexpr int catalog_capacity = last_manual + 1;

    constexpr bool is_native_slot(int64_t slot)
    {
        return slot >= autosave && slot <= last_manual;
    }

    constexpr bool is_manual_slot(int64_t slot)
    {
        return slot >= first_manual && slot <= last_manual;
    }

    constexpr bool is_agent_test_slot(int64_t slot)
    {
        return slot >= first_agent_test && slot <= last_manual;
    }
}
