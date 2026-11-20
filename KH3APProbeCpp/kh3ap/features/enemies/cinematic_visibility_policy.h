#pragma once

#include <array>
#include <string_view>

namespace kh3ap::enemy_remap_detail
{
    inline constexpr std::wstring_view cactuar_intro_camera =
        L"/Game/Levels/ts/ts_02/umap/ts_02_camera_ms141.ts_02_camera_ms141_C";
    inline constexpr std::wstring_view mechanitaur_intro_camera =
        L"/Game/Levels/bx/bx_02/umap/bx_02_camera_ms041s.bx_02_camera_ms041s_C";
    inline constexpr std::wstring_view ice_labyrinth_intro_camera =
        L"/Game/Levels/fz/fz_02/umap/fz_02_camera_ms030.fz_02_camera_ms030_C";
    inline constexpr std::wstring_view ice_labyrinth_second_intro_camera =
        L"/Game/Levels/fz/fz_02/umap/fz_02_camera_ms031.fz_02_camera_ms031_C";
    inline constexpr std::wstring_view ice_labyrinth_third_intro_camera =
        L"/Game/Levels/fz/fz_02/umap/fz_02_camera_ms032.fz_02_camera_ms032_C";
    inline constexpr std::wstring_view ice_labyrinth_fourth_intro_camera =
        L"/Game/Levels/fz/fz_02/umap/fz_02_camera_ms033.fz_02_camera_ms033_C";

    struct IntroRevealRule
    {
        std::wstring_view camera;
        std::wstring_view entity_prefix;
        std::array<std::wstring_view, 8> actor_names;
    };

    // Only reveal the remapped actors authored for this particular introduction.
    inline constexpr IntroRevealRule intro_reveal_rules[] = {
        {cactuar_intro_camera,
         L"/Game/Levels/ts/ts_02/umap/ts_02_enemy_07.ts_02_enemy_07:PersistentLevel.",
         {L"e_ex018_Pawn9_44", L"e_ex033_Pawn7_48", L"e_ex032_Pawn_46"}},
        {mechanitaur_intro_camera,
         L"/Game/Levels/bx/bx_02/umap/bx_02_enemy_13.bx_02_enemy_13:PersistentLevel.",
         {L"e_ex093_Pawn_1", L"e_ex093_Pawn2", L"e_ex093_Pawn3"}},
        {ice_labyrinth_intro_camera,
         L"/Game/Levels/fz/fz_02/umap/fz_02_enemy_icicle_01.fz_02_enemy_icicle_01:PersistentLevel.",
         {L"e_ex114_Pawn_1", L"e_ex114_Pawn_2", L"e_ex114_Pawn_3",
             L"e_ex114_Pawn_7", L"e_ex114_Pawn_8", L"e_ex114_Pawn_9"}},
        {ice_labyrinth_second_intro_camera,
         L"/Game/Levels/fz/fz_02/umap/fz_02_enemy_icicle_02.fz_02_enemy_icicle_02:PersistentLevel.",
         {L"e_ex114_Pawn_10", L"e_ex114_Pawn_11", L"e_ex114_Pawn_12",
             L"e_ex114_Pawn_13", L"e_ex114_Pawn_14", L"e_ex114_Pawn_15"}},
        {ice_labyrinth_third_intro_camera,
         L"/Game/Levels/fz/fz_02/umap/fz_02_enemy_icicle_03.fz_02_enemy_icicle_03:PersistentLevel.",
         {L"e_ex114_Pawn232", L"e_ex114_Pawn286", L"e_ex114_Pawn352",
             L"e_ex114_Pawn429", L"e_ex114_Pawn523", L"e_ex114_Pawn524"}},
        {ice_labyrinth_fourth_intro_camera,
         L"/Game/Levels/fz/fz_02/umap/fz_02_enemy_icicle_04.fz_02_enemy_icicle_04:PersistentLevel.",
         {L"e_ex114_Pawn162", L"e_ex114_Pawn231", L"e_ex114_Pawn285",
             L"e_ex114_Pawn351", L"e_ex114_Pawn428", L"e_ex114_Pawn522",
             L"e_ex114_Pawn525", L"e_ex114_Pawn526"}},
    };

    constexpr const IntroRevealRule* find_intro_reveal_rule(std::wstring_view camera)
    {
        for (const auto& rule : intro_reveal_rules)
        {
            if (rule.camera == camera)
                return &rule;
        }
        return nullptr;
    }

    constexpr bool is_replacement_reveal_camera(std::wstring_view camera)
    {
        return find_intro_reveal_rule(camera) != nullptr;
    }

    constexpr bool is_intro_reveal_entity(std::wstring_view camera, std::wstring_view path)
    {
        const auto* rule = find_intro_reveal_rule(camera);
        if (!rule || !path.starts_with(rule->entity_prefix))
            return false;
        const auto name = path.substr(rule->entity_prefix.size());
        if (name.empty())
            return false;
        for (const auto actor : rule->actor_names)
        {
            if (actor == name)
                return true;
        }
        return false;
    }
}
