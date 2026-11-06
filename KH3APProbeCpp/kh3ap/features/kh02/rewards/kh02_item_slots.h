#pragma once
#include <cstdint>

namespace kh3ap::kh02rewards
{
    constexpr uint8_t item_slot_available = 1;
    constexpr uint8_t item_slot_fixed = 2;

    // Keep unavailable slots closed and preserve reload/empty state. The native
    // setter refuses fixed slots even when the camp lists replacement items.
    constexpr auto editable_item_slot_flags(uint8_t flags) -> uint8_t
    {
        return flags & item_slot_available ? flags & ~item_slot_fixed : flags;
    }
} // namespace kh3ap::kh02rewards
