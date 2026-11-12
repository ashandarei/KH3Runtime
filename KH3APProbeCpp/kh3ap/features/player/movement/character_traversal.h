#pragma once
#include "kh3ap/features/player/player_state_ids.h"
#include <string_view>

namespace RC::Unreal
{
    class UObject;
}

namespace kh3ap::character_traversal
{
    inline constexpr auto gigas_animation_set_path =
        L"/Game/Character/pc/p_ex001/anm/AnimSet/world/p_ex001_AnimSet_TSGigas.p_ex001_AnimSet_TSGigas";
    // ETresStateID entries shared by the native exploration controllers.
    inline constexpr int pole_turn_state = player_state_ids::pole_turn;
    inline constexpr int open_chest_state = player_state_ids::open_treasure_box;
    inline constexpr int swim_state = player_state_ids::swim;
    inline constexpr int special_ship_state = player_state_ids::special_ship;
    inline constexpr int hopping_state = player_state_ids::hopping;
    inline constexpr int hold_camera_state = player_state_ids::hold_camera;
    inline constexpr int save_menu_state = player_state_ids::save_menu;

    bool initialize();
    void capture_sora(RC::Unreal::UObject *pawn);
    void apply_movement(RC::Unreal::UObject *pawn);
    bool prepare(RC::Unreal::UObject *pawn);
    bool ready(RC::Unreal::UObject *pawn);
    bool rail_ready(RC::Unreal::UObject *pawn);
    bool barrel_spin_ready(RC::Unreal::UObject *pawn);
    bool route(int controller, std::wstring_view key, int event, bool gamepad);
} // namespace kh3ap::character_traversal
