#include "kh02_tide_reference.h"
#include "kh02_enemy_stat_table.h"
#include "kh02_tide_balance.h"
#include "kh02_boss_balance.h"
#include "kh3ap/core/reflection/reflection_core.h"

namespace kh3ap
{
    namespace
    {
        constexpr std::size_t expected_tide_phase_rows = 2;

        void capture_combat_fields(Kh02EnemyStatTable& reference, void* reference_row, Kh02EnemyStatTable& destination,
                                   void* destination_row, Kh02EnemyStatRow& row)
        {
            // Copy combat tuning by matching row name. Keep the source AI,
            // attacks, collision, phase gates and completion graph intact.
            for (const auto* field :
                 {L"m_AttackPower", L"m_MagicPower", L"m_DefensePower", L"m_DamageMin", L"m_DamageMax",
                  L"m_AttrResistPhysical", L"m_AttrResistFire", L"m_AttrResistBlizzard", L"m_AttrResistThunder",
                  L"m_AttrResistWater", L"m_AttrResistAero", L"m_AttrResistDark", L"m_AttrResistNoType",
                  L"m_ResistRapidFire"})
            {
                const int original =
                    *destination.field(field, L"IntProperty")->ContainerPtrToValuePtr<int>(destination_row);
                const int adapted = *reference.field(field, L"IntProperty")->ContainerPtrToValuePtr<int>(reference_row);
                row.combat.push_back({field, original, adapted});
            }
        }

    } // namespace

    void kh02_prepare_tide_reference(RC::Unreal::UObject* native_tide, RC::Unreal::UObject* base_table,
                                     std::vector<Kh02EnemyStatRow>& rows, int& tide_reference_base_hp)
    {

        Kh02EnemyStatTable reference(native_tide, L"/Script/TresGame.TresChrBaseParamDataTable");
        Kh02EnemyStatTable base(base_table, L"/Script/TresGame.TresEnemyBaseStatusDataTable");
        const auto reference_key =
            Kh02EnemyStatTable::row_key(std::to_wstring(kh02_tide_balance::reference_level).c_str());
        auto* reference_level_row = base.find(reference_key);
        if (!reference_level_row)
        {
            throw std::runtime_error("kh02_tide_reference_level_missing");
        }
        tide_reference_base_hp =
            *base.field(L"m_BaseHP", L"IntProperty")->ContainerPtrToValuePtr<int>(reference_level_row);
        auto adapted_rows = rows;
        size_t mapped_rows = 0;
        float tide_authored_rate = 0;
        int tide_reference_hp = 0;
        const auto exposed_core_key = Kh02EnemyStatTable::row_key(L"CoreDispOn");
        for (auto& row : adapted_rows)
        {
            auto* table = row.table.Get();
            if (!is_uobject_valid_fast(table) ||
                table->GetPathName() !=
                    L"/Game/Zlueprints/Enemy/e_dw401/DataTable/e_dw401_BaseDataTable.e_dw401_BaseDataTable")
            {
                continue;
            }
            Kh02EnemyStatTable destination(table, L"/Script/TresGame.TresChrBaseParamDataTable");
            auto* destination_row = destination.find(row.name);
            auto* reference_row = reference.find(row.name);
            if (!destination_row || !reference_row)
            {
                throw std::runtime_error("kh02_tide_reference_row_missing");
            }
            row.reference_hp =
                *reference.field(L"m_MaxHitPoint", L"IntProperty")->ContainerPtrToValuePtr<int>(reference_row);
            if (!kh02_tide_balance::scaled_hp(tide_reference_base_hp, row.reference_hp, tide_reference_base_hp))
            {
                throw std::runtime_error("kh02_tide_reference_hp_range");
            }
            if (row.name == exposed_core_key)
            {
                tide_authored_rate = row.rate;
                tide_reference_hp = row.reference_hp;
            }
            capture_combat_fields(reference, reference_row, destination, destination_row, row);
            ++mapped_rows;
        }
        if (mapped_rows != expected_tide_phase_rows)
        {
            throw std::runtime_error("kh02_tide_reference_row_count");
        }
        if (!kh02_boss_balance::reference_hp(tide_authored_rate, tide_authored_rate, tide_reference_hp))
        {
            throw std::runtime_error("kh02_boss_tide_anchor_missing");
        }
        for (auto& row : adapted_rows)
        {
            auto* table = row.table.Get();
            if (!is_uobject_valid_fast(table))
            {
                throw std::runtime_error("kh02_boss_table_unavailable");
            }
            if (!kh02_boss_balance::uses_tide_ratio(table->GetPathName()))
            {
                continue;
            }
            const auto boss_reference_hp =
                kh02_boss_balance::encounter_reference_hp(table->GetPathName(), row.rate, tide_authored_rate,
                                                           tide_reference_hp);
            if (!boss_reference_hp)
            {
                throw std::runtime_error("kh02_boss_reference_hp_range");
            }
            row.reference_hp = *boss_reference_hp;
        }
        rows = std::move(adapted_rows);
    }
} // namespace kh3ap
