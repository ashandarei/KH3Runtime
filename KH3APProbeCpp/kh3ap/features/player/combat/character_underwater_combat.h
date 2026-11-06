#pragma once
#include "kh3ap/features/player/player_state_ids.h"

namespace RC::Unreal
{
    class UObject;
}

namespace kh3ap::character_underwater_combat
{
    inline constexpr int state_id = player_state_ids::combo_swim;
    inline constexpr auto animation_set_path =
        L"/Game/Character/pc/p_ex001/anm/AnimSet/world/p_ex001_AnimSet_CA.p_ex001_AnimSet_CA";
    inline constexpr auto vertical_projectile_path =
        L"/Game/Blueprints/Projectile/Swim/TresProj_VerticleWave.TresProj_VerticleWave_C";
    inline constexpr auto pulse_projectile_path =
        L"/Game/Blueprints/Projectile/Swim/TresProj_PulseWave.TresProj_PulseWave_C";

    void capture_sora(RC::Unreal::UObject* pawn);
    bool prepare(RC::Unreal::UObject* pawn);
} // namespace kh3ap::character_underwater_combat
