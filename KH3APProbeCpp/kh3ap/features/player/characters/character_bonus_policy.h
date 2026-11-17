#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <algorithm>

namespace kh3ap::character_bonuses
{
    inline constexpr size_t record_size = 0x9c0;
    inline constexpr size_t bonus_offset = 0x960;
    inline constexpr std::array<uint8_t, 12> shared_kinds{1, 2, 3, 4, 5, 8, 9, 10, 11, 16, 17, 18};

    // Native records keep base capacities separately from earned VBonus counts.
    // Copy progression, never equipment, item quantities or another character's magic.
    inline void synchronize(std::span<uint8_t, record_size> target,
                            std::span<const uint8_t, record_size> sora)
    {
        for (size_t index = 0; index < 3; ++index)
        {
            if (target[3 + index] > 8)
            {
                throw std::runtime_error("character_bonus_slot_capacity_invalid");
            }
        }
        for (const auto kind : shared_kinds)
        {
            target[bonus_offset + kind] = sora[bonus_offset + kind];
        }
        for (size_t index = 0; index < 3; ++index)
        {
            const auto capacity = std::min(8, target[3 + index] + sora[bonus_offset + 16 + index]);
            for (size_t slot = 0; slot < 8; ++slot)
            {
                auto& flags = target[0x9c + index * 0x40 + slot * 8];
                flags = static_cast<uint8_t>((flags & ~1u) | (slot < capacity ? 1u : 0u));
            }
        }
    }
}
