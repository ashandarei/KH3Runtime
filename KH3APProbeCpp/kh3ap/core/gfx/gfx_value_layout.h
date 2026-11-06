#pragma once

#include <cstddef>
#include <cstdint>

namespace kh3ap::gfx_native_value
{
    // Shipping GFx value representation shared by the verified native readers.
    inline constexpr std::uint32_t value_type_mask = 0x8f;
    inline constexpr std::uint32_t object_value_type = 8;
    inline constexpr std::uint32_t display_object_value_type = 10;
    inline constexpr std::size_t interface_offset = 0x50;
    inline constexpr std::size_t type_offset = 0x58;
    inline constexpr std::size_t value_offset = 0x60;
} // namespace kh3ap::gfx_native_value
