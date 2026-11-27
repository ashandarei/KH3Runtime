#pragma once
#include <array>
#include <cstdint>

namespace kh3ap::combat_swap_detail
{
    struct CameraDistance
    {
        float current{}, battle{}, normal{};
    };

    struct CameraOrbit
    {
        std::array<float, 3> target_rotation{}, angular_delta{}, pivot{}, target_delta{},
            last_target{};
        std::array<float, 3> camera_local_offset{};
        float recenter_wait{}, input_wait{}, reset_blend{};
        uint8_t reset_rotation{};
        uint8_t rotation_lerp{};
    };

    struct CameraView
    {
        std::array<float, 3> location;
        std::array<float, 3> rotation;
        float fov;
    };

    static_assert(sizeof(CameraView) == 28);
} // namespace kh3ap::combat_swap_detail
