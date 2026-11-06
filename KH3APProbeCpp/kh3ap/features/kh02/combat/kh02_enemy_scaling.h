#pragma once
#include "kh02_enemy_stat_rows.h"

namespace RC::Unreal
{
    class UObject;
}

namespace kh3ap
{
    struct Kh02EnemyScalingResult
    {
        int level;
        int base_hp;
    };

    auto kh02_apply_enemy_level(int next, RC::Unreal::UObject* base_table, const std::vector<Kh02EnemyStatRow>& rows,
                                int tide_reference_base_hp) -> Kh02EnemyScalingResult;
} // namespace kh3ap
