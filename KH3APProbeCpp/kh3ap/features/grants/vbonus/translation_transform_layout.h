#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstring>
#include <span>

namespace kh3ap::translation_transform
{
    // Shipping FTransform: padded Rotation, Translation and Scale3D float groups.
    inline constexpr std::size_t component_count = 12;
    inline constexpr std::size_t storage_bytes = component_count * sizeof(float);
    inline constexpr std::size_t rotation_w_index = 3;
    inline constexpr std::size_t translation_x_index = 4;
    inline constexpr std::size_t translation_y_index = 5;
    inline constexpr std::size_t translation_z_index = 6;
    inline constexpr std::size_t scale_x_index = 8;
    inline constexpr std::size_t scale_y_index = 9;
    inline constexpr std::size_t scale_z_index = 10;

    inline auto write(std::span<std::byte> storage, double x, double y, double z) -> bool
    {
        if (storage.size() < storage_bytes)
        {
            return false;
        }
        std::array<float, component_count> components{};
        components[rotation_w_index] = 1.0f;
        components[translation_x_index] = static_cast<float>(x);
        components[translation_y_index] = static_cast<float>(y);
        components[translation_z_index] = static_cast<float>(z);
        components[scale_x_index] = 1.0f;
        components[scale_y_index] = 1.0f;
        components[scale_z_index] = 1.0f;
        std::fill(storage.begin(), storage.end(), std::byte{});
        std::memcpy(storage.data(), components.data(), storage_bytes);
        return true;
    }
} // namespace kh3ap::translation_transform
