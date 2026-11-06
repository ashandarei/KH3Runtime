#pragma once

#include <cstdint>

namespace kh3ap::space
{
    inline constexpr unsigned placed_sphere_count = 9;
    inline constexpr uint16_t first_sphere_mask = 0x8000;

    constexpr bool sphere_completion_notification(unsigned id, bool opened)
    {
        return opened && id < placed_sphere_count;
    }

    // Native treasure flags are a MSB-first uint16 bitset. Only the nine placed
    // GM01-03 spheres have AP locations; GM04/reserved bits must never emit checks.
    constexpr bool sphere_opened(uint16_t flags, unsigned id)
    {
        return id < placed_sphere_count && (flags & (first_sphere_mask >> id)) != 0;
    }
}
