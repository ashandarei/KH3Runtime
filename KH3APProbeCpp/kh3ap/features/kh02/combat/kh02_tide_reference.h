#pragma once
#include "kh02_enemy_stat_rows.h"

namespace RC::Unreal
{
    class UObject;
}

namespace kh3ap
{
    void kh02_prepare_tide_reference(RC::Unreal::UObject* native_tide, RC::Unreal::UObject* base_table,
                                     std::vector<Kh02EnemyStatRow>& rows, int& tide_reference_base_hp);
}
