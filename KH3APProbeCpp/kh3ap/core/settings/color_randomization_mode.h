#pragma once

#include <cstdint>
#include <optional>

namespace kh3ap
{
    // Stable IDs shared by seed data, persisted settings and the Palettesanity menu.
    enum class ColorRandomizationMode : int32_t
    {
        Off = 0,
        Coordinated = 1,
        Wild = 2,
    };

    constexpr auto color_randomization_mode_id(ColorRandomizationMode mode) -> int32_t
    {
        return static_cast<int32_t>(mode);
    }

    constexpr auto color_randomization_mode_from_id(int64_t value)
        -> std::optional<ColorRandomizationMode>
    {
        if (value < color_randomization_mode_id(ColorRandomizationMode::Off) ||
            value > color_randomization_mode_id(ColorRandomizationMode::Wild))
        {
            return std::nullopt;
        }
        return static_cast<ColorRandomizationMode>(value);
    }
} // namespace kh3ap
