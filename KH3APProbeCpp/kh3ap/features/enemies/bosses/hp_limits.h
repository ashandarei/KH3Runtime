#pragma once

#include "kh3ap/features/enemies/remap_runtime.h"

namespace kh3ap::enemy_remap_detail
{
    void install_boss_hp_limits();
    void install_luxord_partner_hp_gate(UObject* pawn, const RuntimeBossEntityBinding& binding);
    void prepare_luxord_partner_hp_gate(UObject* pawn);
    void register_luxord_source_entity(UObject* entity, std::wstring_view path);
}
