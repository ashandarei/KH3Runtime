#pragma once

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>

namespace kh3ap
{
    inline constexpr int32_t experience_half_steps_per_unit = 2;
    inline constexpr std::size_t default_experience_multiplier_choice = 1;
    inline constexpr double experience_multiplier_tolerance = 0.000001;

    // Archipelago's KH3 option stores the multiplier in half-step units:
    // 1 = 0.5x; 2, 4, 6, 8, 10 = 1x through 5x; 20 = 10x; 40 = 20x.
    inline constexpr std::array<int32_t, 8>
            runtime_experience_multiplier_half_steps{
                    1,
                    2,
                    4,
                    6,
                    8,
                    10,
                    20,
                    40,
            };

    [[nodiscard]] constexpr auto is_supported_experience_multiplier_half_steps(
            int32_t half_steps) -> bool
    {
        for (const int32_t candidate :
             runtime_experience_multiplier_half_steps)
        {
            if (candidate == half_steps)
            {
                return true;
            }
        }
        return false;
    }

    [[nodiscard]] constexpr auto experience_multiplier_choice_index(
            int32_t half_steps) -> std::size_t
    {
        for (std::size_t index = 0;
             index < runtime_experience_multiplier_half_steps.size();
             ++index)
        {
            if (runtime_experience_multiplier_half_steps[index] == half_steps)
            {
                return index;
            }
        }
        return default_experience_multiplier_choice;
    }

    [[nodiscard]] inline auto experience_multiplier_half_steps_from_value(
            double multiplier) -> std::optional<int32_t>
    {
        if (!std::isfinite(multiplier) || multiplier <= 0.0)
        {
            return std::nullopt;
        }
        const long long half_steps = std::llround(multiplier * experience_half_steps_per_unit);
        if (half_steps < std::numeric_limits<int32_t>::min() ||
            half_steps > std::numeric_limits<int32_t>::max())
        {
            return std::nullopt;
        }
        const int32_t candidate = static_cast<int32_t>(half_steps);
        if (!is_supported_experience_multiplier_half_steps(candidate) ||
            std::abs(multiplier - static_cast<double>(candidate) / experience_half_steps_per_unit) >
                    experience_multiplier_tolerance)
        {
            return std::nullopt;
        }
        return candidate;
    }

    // Vanilla base awards are scaled once, before KH3's difficulty and EXP
    // boost modifiers. Positive halves round away from zero using integer math.
    [[nodiscard]] constexpr auto scale_experience_for_runtime_award(
            int32_t base_experience,
            int32_t runtime_multiplier_half_steps) -> std::optional<int32_t>
    {
        if (base_experience < 0 ||
            !is_supported_experience_multiplier_half_steps(
                    runtime_multiplier_half_steps))
        {
            return std::nullopt;
        }

        const int64_t numerator =
                static_cast<int64_t>(base_experience) *
                static_cast<int64_t>(runtime_multiplier_half_steps);
        constexpr int64_t rounding_bias = experience_half_steps_per_unit / 2;
        const int64_t scaled_experience =
                (numerator + rounding_bias) / experience_half_steps_per_unit;
        if (scaled_experience > std::numeric_limits<int32_t>::max())
        {
            return std::nullopt;
        }
        return static_cast<int32_t>(scaled_experience);
    }
}
