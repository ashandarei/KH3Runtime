#pragma once
#include "kh3ap/core/platform/game_build.h"
#include <array>
#include <cstddef>
#include <cstdint>

namespace kh3ap::chest_activation {
struct Profile { uint32_t rva, base_setter; };
constexpr Profile select(uint32_t timestamp, uint32_t size) {
    if (kh3ap::native_build::egs.matches(timestamp, size)) return {0x123e130, 0x11efa80};
    if (kh3ap::native_build::steam.matches(timestamp, size)) return {0xfa3930, 0xf488c0};
    return {};
}
// Complete TresTreasureBox activation override, including its tail call to
// TresGimmickActor's setter. Both builds use the same ABI and opened-bit guard.
constexpr auto signature(Profile profile) {
    std::array<uint8_t, 33> bytes{0x0f,0xb6,0xc2,0x84,0xd2,0x74,0x12,0xf6,0x81,
        0x28,0x07,0,0,0x02,0x0f,0xb6,0xc2,0xba,0,0,0,0,0x0f,0x45,0xc2,
        0x0f,0xb6,0xd0,0xe9};
    const uint32_t displacement = profile.base_setter - (profile.rva + static_cast<uint32_t>(bytes.size()));
    for (size_t i=0; i<4; ++i) bytes[29+i] = static_cast<uint8_t>(displacement >> (i*8));
    return bytes;
}
}
