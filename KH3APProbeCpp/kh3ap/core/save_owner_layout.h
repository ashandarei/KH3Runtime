#pragma once

#include <cstddef>

namespace kh3ap::native_save_owner_layout
{
    // Shared shipping accessor/owner fields, admitted by each reader's native guards.
    inline constexpr std::size_t accessor_instruction_bytes = 13;
    inline constexpr std::size_t accessor_displacement_offset = 9;
    inline constexpr std::size_t engine_singleton_offset = 0x230;
    inline constexpr std::size_t singleton_save_owner_offset = 0x50;
} // namespace kh3ap::native_save_owner_layout
