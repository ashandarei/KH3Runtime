#pragma once
#include <array>
#include <cmath>
#include <optional>

namespace kh3ap
{
    using Kh02PillarSizeVector = std::array<float, 3>;
    using Kh02PillarSizeValues = std::array<Kh02PillarSizeVector, 4>;
    inline constexpr Kh02PillarSizeVector kh02_pillar_authored_size{10.0f, 1.5f, 1.0f};
    inline constexpr Kh02PillarSizeVector kh02_pillar_native_size{1.0f, 1.5f, 10.0f};

    inline auto kh02_pillar_size_replacement(const Kh02PillarSizeValues& current) noexcept
        -> std::optional<Kh02PillarSizeValues>
    {
        const Kh02PillarSizeValues authored{kh02_pillar_authored_size, kh02_pillar_authored_size,
                                            kh02_pillar_authored_size, kh02_pillar_authored_size};
        const Kh02PillarSizeValues repaired{kh02_pillar_native_size, kh02_pillar_native_size, kh02_pillar_native_size,
                                            kh02_pillar_native_size};
        if (current != authored && current != repaired)
        {
            return std::nullopt;
        }
        return repaired;
    }

    inline constexpr float kh02_pillar_origin_tolerance = 0.25f;

    inline auto kh02_pillar_origin_is_duplicated(const std::array<float, 3>& relative,
                                                 const std::array<float, 3>& parent_world,
                                                 bool absolute_location) noexcept -> bool
    {
        if (absolute_location)
        {
            return false;
        }
        bool already_zero = true;
        for (std::size_t axis = 0; axis < relative.size(); ++axis)
        {
            if (!std::isfinite(relative[axis]) || !std::isfinite(parent_world[axis]) ||
                std::abs(relative[axis] - parent_world[axis]) > kh02_pillar_origin_tolerance)
            {
                return false;
            }
            already_zero = already_zero && std::abs(relative[axis]) <= kh02_pillar_origin_tolerance;
        }
        return !already_zero;
    }
} // namespace kh3ap
