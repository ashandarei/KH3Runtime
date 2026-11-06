#pragma once

#include <Unreal/NameTypes.hpp>
#include <cstdint>
#include "player_ids.h"

namespace RC::Unreal
{
    class UObject;
}

namespace kh3ap::player_animation_support
{
    using PlayerId = kh3ap::PlayerId;

    inline constexpr auto sora_animation_set_path =
        L"/Game/Character/pc/p_ex001/anm/p_ex001_AnimSet.p_ex001_AnimSet";
    inline constexpr auto kairi_idle_animation_set_path =
        L"/Game/Character/npc/n_ex005/anm/n_ex005_GM_Body_AnimSet.n_ex005_GM_Body_AnimSet";
    inline constexpr auto sora_attack_data_path =
        L"/Game/DataTable/Player/Attack/p_ex001_AttackData.p_ex001_AttackData";
    inline constexpr auto kairi_attack_data_path =
        L"/Game/DataTable/Player/Attack/p_ex006_AttackData.p_ex006_AttackData";

    // TresAnimSet::Anims stores an eight-byte FName and a UObject pointer on Win64.
    struct AnimationEntry
    {
        RC::Unreal::FName name;
        RC::Unreal::UObject* asset;
    };
    static_assert(sizeof(AnimationEntry) == 16);

    // Verify EGS CL1065258 and both native skeleton-remapping entry points.
    bool supported_skeleton_layout();
    void allow_animation_skeleton(RC::Unreal::UObject* animation_instance,
                                  RC::Unreal::UObject* animation);
} // namespace kh3ap::player_animation_support
