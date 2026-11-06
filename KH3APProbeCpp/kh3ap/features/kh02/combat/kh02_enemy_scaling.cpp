#include "kh02_enemy_scaling.h"
#include "kh02_enemy_scaling_policy.h"
#include "kh02_enemy_stat_table.h"
#include "kh02_tide_balance.h"

namespace kh3ap
{
    auto kh02_apply_enemy_level(int next, RC::Unreal::UObject* base_table, const std::vector<Kh02EnemyStatRow>& rows,
                                int tide_reference_base_hp) -> Kh02EnemyScalingResult
    {

        if (next < 1 || next > kh02_maximum_battle_level)
        {
            throw std::runtime_error("kh02_stats_level_range");
        }
        Kh02EnemyStatTable base(base_table, L"/Script/TresGame.TresEnemyBaseStatusDataTable");
        const auto name = Kh02EnemyStatTable::row_key(std::to_wstring(next).c_str());
        auto* row = base.find(name);
        if (!row)
        {
            throw std::runtime_error("kh02_stats_level_row_missing");
        }
        const int hp = *base.field(L"m_BaseHP", L"IntProperty")->ContainerPtrToValuePtr<int>(row);
        if (hp < 1 || hp > kh02_maximum_native_hit_points)
        {
            throw std::runtime_error("kh02_stats_base_hp_range");
        }
        // Validate every destination before changing any row. The authored rate is
        // cached once, so repeated battle-level updates cannot compound scaling.
        std::vector<std::pair<int*, int>> writes;
        for (const auto& record : rows)
        {
            Kh02EnemyStatTable destination(record.table.Get(), L"/Script/TresGame.TresChrBaseParamDataTable");
            auto* destination_row = destination.find(record.name);
            if (!destination_row)
            {
                throw std::runtime_error("kh02_stats_enemy_row_missing");
            }
            const auto tide_hp = record.reference_hp
                                     ? kh02_tide_balance::scaled_hp(hp, record.reference_hp, tide_reference_base_hp)
                                     : std::optional<int>{};
            if (record.reference_hp && !tide_hp)
            {
                throw std::runtime_error("kh02_tide_scaled_hp_range");
            }
            const int scaled_hp = kh02_scaled_enemy_hp(hp, record.rate, tide_hp);
            writes.emplace_back(
                destination.field(L"m_MaxHitPoint", L"IntProperty")->ContainerPtrToValuePtr<int>(destination_row),
                scaled_hp);
            for (const auto& value : record.combat)
            {
                writes.emplace_back(
                    destination.field(value.field, L"IntProperty")->ContainerPtrToValuePtr<int>(destination_row),
                    value.adapted);
            }
        }
        for (auto [destination_row, hp_value] : writes)
        {
            *destination_row = hp_value;
        }
        return {next, hp};
    }
} // namespace kh3ap
