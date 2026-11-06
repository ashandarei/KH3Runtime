#pragma once

#include <cstdint>

namespace kh3ap::memory_protection
{
    // The low byte holds the Win32 base protection mode, without modifier flags.
    inline constexpr uint32_t base_mode_mask = 0xff;
} // namespace kh3ap::memory_protection
