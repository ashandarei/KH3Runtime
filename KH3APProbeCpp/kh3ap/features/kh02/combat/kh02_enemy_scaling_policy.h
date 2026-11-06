#pragma once
#include <algorithm>
#include <cmath>
#include <optional>
#include <stdexcept>

namespace kh3ap
{
    inline constexpr int kh02_maximum_battle_level = 99;
    inline constexpr int kh02_maximum_native_hit_points = 100'000;
    inline constexpr double kh02_maximum_calculated_stat = 1'000'000;

    inline auto kh02_scaled_enemy_hp(int hp, float rate, std::optional<int> tide_hp = {}) -> int
    {
        const double calculated = tide_hp ? *tide_hp : double(hp) * rate;
        if (!std::isfinite(calculated) || calculated > kh02_maximum_calculated_stat)
        {
            throw std::runtime_error("kh02_stats_scaled_hp_range");
        }
        return std::max(1, int(calculated));
    }
} // namespace kh3ap
