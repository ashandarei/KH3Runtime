#pragma once

#include <cstddef>

namespace kh3ap::character_native::layout
{
    // Shared by the verified EGS and Steam character implementations. Callers
    // must still verify their native sites before accessing these fields.
    inline constexpr size_t player_state_interface_offset = 0x450;
    inline constexpr size_t character_kind_offset = 0x5f0;
    inline constexpr size_t hud_status_flags_offset = 0x3fc;
    inline constexpr size_t glide_heading_vtable_offset = 0xda0;
    inline constexpr size_t controller_glide_duration_offset = 0x854;
}
