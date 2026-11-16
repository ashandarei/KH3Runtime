#include "kh3ap/features/enemies/cinematic_visibility_policy.h"

using namespace kh3ap::enemy_remap_detail;
// Compile-time checks remain enabled in Release builds.
static_assert(is_intro_reveal_entity(mechanitaur_intro_camera,
    L"/Game/Levels/bx/bx_02/umap/bx_02_enemy_13.bx_02_enemy_13:PersistentLevel.e_ex093_Pawn_1"));
static_assert(is_intro_reveal_entity(mechanitaur_intro_camera,
    L"/Game/Levels/bx/bx_02/umap/bx_02_enemy_13.bx_02_enemy_13:PersistentLevel.e_ex093_Pawn2"));
static_assert(is_intro_reveal_entity(mechanitaur_intro_camera,
    L"/Game/Levels/bx/bx_02/umap/bx_02_enemy_13.bx_02_enemy_13:PersistentLevel.e_ex093_Pawn3"));
static_assert(!is_intro_reveal_entity(mechanitaur_intro_camera,
    L"/Game/Levels/bx/bx_02/umap/bx_02_enemy_13.bx_02_enemy_13:PersistentLevel.e_ex093_Pawn4"));
static_assert(!is_intro_reveal_entity(cactuar_intro_camera,
    L"/Game/Levels/bx/bx_02/umap/bx_02_enemy_13.bx_02_enemy_13:PersistentLevel.e_ex093_Pawn2"));
static_assert(!is_intro_reveal_entity(L"unrelated_camera",
    L"/Game/Levels/bx/bx_02/umap/bx_02_enemy_13.bx_02_enemy_13:PersistentLevel.e_ex093_Pawn2"));
static_assert(!is_intro_reveal_entity(mechanitaur_intro_camera,
    L"/Game/Levels/bx/bx_02/umap/bx_02_enemy_14.bx_02_enemy_14:PersistentLevel.e_ex093_Pawn2"));
static_assert(is_intro_reveal_entity(cactuar_intro_camera,
    L"/Game/Levels/ts/ts_02/umap/ts_02_enemy_07.ts_02_enemy_07:PersistentLevel.e_ex018_Pawn9_44"));
static_assert(is_intro_reveal_entity(cactuar_intro_camera,
    L"/Game/Levels/ts/ts_02/umap/ts_02_enemy_07.ts_02_enemy_07:PersistentLevel.e_ex033_Pawn7_48"));
static_assert(is_intro_reveal_entity(cactuar_intro_camera,
    L"/Game/Levels/ts/ts_02/umap/ts_02_enemy_07.ts_02_enemy_07:PersistentLevel.e_ex032_Pawn_46"));
static_assert(is_replacement_reveal_camera(ice_labyrinth_intro_camera));
static_assert([] {
    constexpr std::wstring_view prefix =
        L"/Game/Levels/fz/fz_02/umap/fz_02_enemy_icicle_01.fz_02_enemy_icicle_01:PersistentLevel.";
    constexpr std::wstring_view entities[] = {
        L"/Game/Levels/fz/fz_02/umap/fz_02_enemy_icicle_01.fz_02_enemy_icicle_01:PersistentLevel.e_ex114_Pawn_1",
        L"/Game/Levels/fz/fz_02/umap/fz_02_enemy_icicle_01.fz_02_enemy_icicle_01:PersistentLevel.e_ex114_Pawn_2",
        L"/Game/Levels/fz/fz_02/umap/fz_02_enemy_icicle_01.fz_02_enemy_icicle_01:PersistentLevel.e_ex114_Pawn_3",
        L"/Game/Levels/fz/fz_02/umap/fz_02_enemy_icicle_01.fz_02_enemy_icicle_01:PersistentLevel.e_ex114_Pawn_7",
        L"/Game/Levels/fz/fz_02/umap/fz_02_enemy_icicle_01.fz_02_enemy_icicle_01:PersistentLevel.e_ex114_Pawn_8",
        L"/Game/Levels/fz/fz_02/umap/fz_02_enemy_icicle_01.fz_02_enemy_icicle_01:PersistentLevel.e_ex114_Pawn_9",
    };
    for (const auto entity : entities)
    {
        if (!is_intro_reveal_entity(ice_labyrinth_intro_camera, entity) ||
            is_intro_reveal_entity(cactuar_intro_camera, entity) ||
            is_intro_reveal_entity(mechanitaur_intro_camera, entity)) return false;
    }
    return !is_intro_reveal_entity(ice_labyrinth_intro_camera, prefix);
}());
static_assert(!is_intro_reveal_entity(ice_labyrinth_intro_camera,
    L"/Game/Levels/fz/fz_02/umap/fz_02_enemy_icicle_01.fz_02_enemy_icicle_01:PersistentLevel.e_ex114_Pawn_10"));
static_assert(!is_intro_reveal_entity(ice_labyrinth_intro_camera,
    L"/Game/Levels/fz/fz_02/umap/fz_02_enemy_icicle_02.fz_02_enemy_icicle_02:PersistentLevel.e_ex114_Pawn_1"));
static_assert(!is_intro_reveal_entity(ice_labyrinth_intro_camera,
    L"/Game/Levels/bx/bx_02/umap/bx_02_enemy_13.bx_02_enemy_13:PersistentLevel.e_ex093_Pawn2"));
int main() {}
