#pragma once
#include <array>
#include <cmath>
#include <optional>
namespace kh3ap {
    inline constexpr int scene_coordinate_magnitude_limit = 10000000;

    using ScenePoint = std::array<float, 3>;
    // Unreal's serialized Rotator is Pitch/Yaw/Roll in degrees. Transform a
    // component-local point into its parent's local space without actor execution.
    inline auto scene_parent_point(ScenePoint point, ScenePoint location, ScenePoint rotation,
                                   ScenePoint scale) -> std::optional<ScenePoint>
    {
        for (const auto& values : {point, location, rotation, scale})
        {
            for (auto value : values)
            {
                if (!std::isfinite(value))
                {
                    return {};
                }
            }
        }
        constexpr double radians = 3.14159265358979323846 / 180.;
        const auto sp = std::sin(rotation[0] * radians), cp = std::cos(rotation[0] * radians);
        const auto sy = std::sin(rotation[1] * radians), cy = std::cos(rotation[1] * radians);
        const auto sr = std::sin(rotation[2] * radians), cr = std::cos(rotation[2] * radians);
        const double x = point[0] * scale[0], y = point[1] * scale[1], z = point[2] * scale[2];
        ScenePoint result{
            static_cast<float>(location[0] + x * cp * cy + y * (sr * sp * cy - cr * sy) -
                               z * (cr * sp * cy + sr * sy)),
            static_cast<float>(location[1] + x * cp * sy + y * (sr * sp * sy + cr * cy) +
                               z * (sr * cy - cr * sp * sy)),
            static_cast<float>(location[2] + x * sp - y * sr * cp + z * cr * cp)};
        for (auto value : result)
        {
            if (!std::isfinite(value) || std::abs(value) > scene_coordinate_magnitude_limit)
            {
                return {};
            }
        }
        return result;
    }
} // namespace kh3ap
