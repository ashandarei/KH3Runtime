#pragma once
#include <array>
#include <cstdint>

namespace kh3ap {
// A value snapshot only: no UObject survives a frame through the map overlay.
struct RuntimeLuckyEmblemMarker {
    int32_t mark_id{};
    std::array<float,3> location{};
};
}
