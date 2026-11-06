#pragma once

#include <algorithm>
#include <cmath>
#include <optional>
#include <string_view>

namespace kh3ap::kh02_boss_balance
{
    // Only imported boss tables participate. Keep regular Heartless, including
    // challenge variants, on their existing authored HP curves.
    constexpr bool uses_tide_ratio(std::wstring_view table_path)
    {
        constexpr std::wstring_view boss_tables[] = {
            L"/Game/Zlueprints/Enemy/e_dw402/DataTable/e_dw402_BaseDataTable.e_dw402_BaseDataTable",
            L"/Game/Zlueprints/Enemy/e_dw402_s0/DataTable/e_dw402_BaseDataTable_s0.e_dw402_BaseDataTable_s0",
            L"/Game/Zlueprints/Enemy/e_dw402_s100/DataTable/e_dw402_BaseDataTable_s100.e_dw402_BaseDataTable_s100",
            L"/Game/Zlueprints/Enemy/e_dw405/DataTable/e_dw405_BaseDataTable.e_dw405_BaseDataTable",
            L"/Game/Zlueprints/Enemy/e_dw405_s1/DataTable/e_dw405_BaseDataTable_s1.e_dw405_BaseDataTable_s1",
            L"/Game/Zlueprints/Enemy/e_dw405_s100/DataTable/e_dw405_BaseDataTable_s100.e_dw405_BaseDataTable_s100",
            L"/Game/Zlueprints/Enemy/e_dw407/DataTable/e_dw407_BaseDataTable.e_dw407_BaseDataTable",
            L"/Game/Zlueprints/Enemy/e_dw407a_s0/DataTable/e_dw407a_BaseDataTable.e_dw407a_BaseDataTable",
            L"/Game/Zlueprints/Enemy/e_dw407b/DataTable/e_dw407b_BaseDataTable.e_dw407b_BaseDataTable",
            L"/Game/Zlueprints/Enemy/e_dw407_s100/DataTable/e_dw407_BaseDataTable_s100.e_dw407_BaseDataTable_s100"};
        for (const auto boss_table : boss_tables)
        {
            if (table_path == boss_table)
            {
                return true;
            }
        }
        return false;
    }

    // Preserve the source boss/Tide HP ratio at the Tide's normalization level.
    // Both rates come from untouched source rows; never feed adapted HP back in.
    inline auto reference_hp(float boss_rate, float tide_rate, int tide_reference_hp) -> std::optional<int>
    {
        constexpr float maximum_authored_rate = 1'000.0f;
        constexpr int maximum_hit_points = 1'000'000;
        if (!std::isfinite(boss_rate) || boss_rate <= 0 || boss_rate > maximum_authored_rate ||
            !std::isfinite(tide_rate) || tide_rate <= 0 || tide_rate > maximum_authored_rate || tide_reference_hp < 1 ||
            tide_reference_hp > maximum_hit_points)
        {
            return std::nullopt;
        }
        const double hit_points = static_cast<double>(tide_reference_hp) * boss_rate / tide_rate;
        if (!std::isfinite(hit_points) || hit_points > maximum_hit_points)
        {
            return std::nullopt;
        }
        return std::max(1, static_cast<int>(hit_points));
    }

    inline auto encounter_reference_hp(std::wstring_view table_path, float boss_rate, float tide_rate,
                                       int tide_reference_hp) -> std::optional<int>
    {
        const auto reference = reference_hp(boss_rate, tide_rate, tide_reference_hp);
        if (!reference)
        {
            return std::nullopt;
        }
        if (table_path ==
            L"/Game/Zlueprints/Enemy/e_dw402_s0/DataTable/e_dw402_BaseDataTable_s0.e_dw402_BaseDataTable_s0")
        {
            // The opening Tower targets four reserve bars and half of the
            // visible 200-HP bar at level 50. Lower levels use the same curve.
            constexpr int opening_tower_level_50_hp = 900;
            return opening_tower_level_50_hp;
        }
        return reference;
    }
} // namespace kh3ap::kh02_boss_balance
