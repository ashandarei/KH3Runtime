#pragma once

#include <cstdint>

namespace kh3ap
{
    // Stable IDs stored in wardrobe selections and progress sidecars.
    enum class WardrobeColorMode : int32_t
    {
        Default = 0,
        SingleColor = 1,
        Gradient = 2,
    };
} // namespace kh3ap
