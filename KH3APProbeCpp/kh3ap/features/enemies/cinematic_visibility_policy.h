#pragma once

#include <string_view>

namespace kh3ap::enemy_remap_detail
{
    inline constexpr std::wstring_view cactuar_intro_camera =
        L"/Game/Levels/ts/ts_02/umap/ts_02_camera_ms141.ts_02_camera_ms141_C";
    inline constexpr std::wstring_view mechanitaur_intro_camera =
        L"/Game/Levels/bx/bx_02/umap/bx_02_camera_ms041s.bx_02_camera_ms041s_C";
    inline constexpr std::wstring_view ice_labyrinth_intro_camera =
        L"/Game/Levels/fz/fz_02/umap/fz_02_camera_ms030.fz_02_camera_ms030_C";

    constexpr bool is_replacement_reveal_camera(std::wstring_view camera)
    {
        return camera == cactuar_intro_camera || camera == mechanitaur_intro_camera ||
            camera == ice_labyrinth_intro_camera;
    }

    // Only the actors revealed by these introductions. Later waves, other
    // encounters and vanilla actors must retain their authored visibility.
    constexpr bool is_intro_reveal_entity(std::wstring_view camera, std::wstring_view path)
    {
        const auto prefix = camera == cactuar_intro_camera
            ? L"/Game/Levels/ts/ts_02/umap/ts_02_enemy_07.ts_02_enemy_07:PersistentLevel."
            : camera == ice_labyrinth_intro_camera
            ? L"/Game/Levels/fz/fz_02/umap/fz_02_enemy_icicle_01.fz_02_enemy_icicle_01:PersistentLevel."
            : L"/Game/Levels/bx/bx_02/umap/bx_02_enemy_13.bx_02_enemy_13:PersistentLevel.";
        if (!is_replacement_reveal_camera(camera) || !path.starts_with(prefix))
        {
            return false;
        }
        const auto name = path.substr(std::wstring_view{prefix}.size());
        if (camera == cactuar_intro_camera)
        {
            return name == L"e_ex018_Pawn9_44" || name == L"e_ex033_Pawn7_48" ||
                name == L"e_ex032_Pawn_46";
        }
        if (camera == ice_labyrinth_intro_camera)
        {
            return name == L"e_ex114_Pawn_1" || name == L"e_ex114_Pawn_2" ||
                name == L"e_ex114_Pawn_3" || name == L"e_ex114_Pawn_7" ||
                name == L"e_ex114_Pawn_8" || name == L"e_ex114_Pawn_9";
        }
        return name == L"e_ex093_Pawn_1" || name == L"e_ex093_Pawn2" || name == L"e_ex093_Pawn3";
    }
}
