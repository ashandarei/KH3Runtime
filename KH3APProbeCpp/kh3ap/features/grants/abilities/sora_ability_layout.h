#pragma once

#include <cstddef>
#include <cstdint>

namespace kh3ap::sora_ability_layout
{
    inline constexpr std::size_t entry_count = 256;
    inline constexpr uint32_t owned_mask = 0x0001;
    inline constexpr uint32_t equipped_mask = 0x0002;
} // namespace kh3ap::sora_ability_layout
