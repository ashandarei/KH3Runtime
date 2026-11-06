#pragma once
#include <algorithm>
#include <cstdint>
#include <limits>
#include <optional>
#include <string_view>

namespace kh3ap::boss_hp_setup
{
    inline constexpr std::wstring_view forest_world = L"/Game/Maps/APKH02Mirrors/APKH02Forest.APKH02Forest";
    inline constexpr std::wstring_view darkside_class =
        L"/Game/Zlueprints/Enemy/e_dw407b/e_dw407b_Pawn.e_dw407b_Pawn_C";
    inline constexpr std::int32_t percent_of_max = 10;
    inline constexpr std::int32_t percent_denominator = 100;
    inline constexpr std::int32_t surviving_hp_floor = 1;
    inline constexpr std::uint64_t damageable_body_reaction = 0;

    // A phase fixture removes ten percent of the current maximum, rounded down
    // to integer HP (at least one). It never asks the damage system to hit.
    inline auto target_hp(std::int64_t current, std::int64_t maximum, std::int64_t percent)
        -> std::optional<std::int32_t>
    {
        if (percent != percent_of_max || current <= surviving_hp_floor || maximum < current ||
            maximum > std::numeric_limits<std::int32_t>::max())
        {
            return std::nullopt;
        }
        const auto reduction = std::max<std::int64_t>(1, maximum * percent_of_max / percent_denominator);
        return static_cast<std::int32_t>(std::max<std::int64_t>(surviving_hp_floor, current - reduction));
    }
} // namespace kh3ap::boss_hp_setup
