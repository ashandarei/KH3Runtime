#pragma once

#include <algorithm>
#include <cstdint>
#include <optional>

namespace kh3ap::kh02_tide_balance
{
    // Balance reference, not a forced encounter level. Both playable heroes
    // continue to use the current native battle level and EnemyBaseStatus curve.
    inline constexpr int reference_level = 50;

    constexpr auto scaled_hp(int current_base_hp, int reference_hp, int reference_base_hp) -> std::optional<int>
    {
        if (current_base_hp < 1 || current_base_hp > 100000 || reference_hp < 1 || reference_hp > 1000000 ||
            reference_base_hp < 1 || reference_base_hp > 100000)
        {
            return std::nullopt;
        }
        const auto result = static_cast<int64_t>(current_base_hp) * reference_hp / reference_base_hp;
        if (result > 1000000)
        {
            return std::nullopt;
        }
        return static_cast<int>(std::max<int64_t>(1, result));
    }
} // namespace kh3ap::kh02_tide_balance
