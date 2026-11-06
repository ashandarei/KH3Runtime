#pragma once
#include "kh3ap/features/player/player_state_ids.h"

namespace RC::Unreal
{
    class UObject;
}

namespace kh3ap::character_barrel_spin
{
    inline constexpr int one_action_state = player_state_ids::one_action;
    bool allows_state(RC::Unreal::UObject *pawn);
    bool selected_barrel(RC::Unreal::UObject *actor, RC::Unreal::UObject *reactor,
                         RC::Unreal::UObject *pawn);
} // namespace kh3ap::character_barrel_spin
