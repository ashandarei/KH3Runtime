#pragma once
#include <cstdint>

namespace kh3ap::space
{
    inline constexpr uintptr_t battle_marker_array_offset = 808;
    inline constexpr uintptr_t battle_marker_slot_stride = 88;
    inline constexpr uintptr_t battle_marker_slot_count = 10;
    // The EGS front-facing helper leaves its unused fourth argument unspecified.
    // Both storefronts always pass the actual slot in the reflected enemy array.
    constexpr int battle_marker_slot_index(uintptr_t owner, uintptr_t slot)
    {
        if (slot < owner)
        {
            return -1;
        }
        const auto offset = slot - owner;
        if (offset < battle_marker_array_offset ||
            offset >=
                battle_marker_array_offset + battle_marker_slot_count * battle_marker_slot_stride ||
            (offset - battle_marker_array_offset) % battle_marker_slot_stride != 0)
        {
            return -1;
        }
        return static_cast<int>((offset - battle_marker_array_offset) / battle_marker_slot_stride);
    }
} // namespace kh3ap::space
