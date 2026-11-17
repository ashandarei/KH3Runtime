#include "kh3ap/features/player/characters/character_travel_policy.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>

int main()
{
    using kh3ap::character_swap::is_tornado_freefall_cinematic;
    constexpr auto tornado = L"/Game/Levels/he/he_06/he_06.HE_06";
    constexpr auto dive_movie = L"/Game/Levels/he/he_06/umap/he_06_camera_ms130."
                                L"he_06_camera_ms130:PersistentLevel.ms130m";
    assert(is_tornado_freefall_cinematic(tornado, dive_movie));
    assert(!is_tornado_freefall_cinematic(tornado, L"ms130"));
    assert(!is_tornado_freefall_cinematic(tornado,
        L"/Game/Levels/he/he_06/umap/he_06_camera_ms130.he_06_camera_ms130:PersistentLevel.ms130s"));
    assert(!is_tornado_freefall_cinematic(L"/Game/Levels/he/he_03/he_03.he_03", dive_movie));
    using kh3ap::character_swap::can_prepare_arrival_under_cover;
    constexpr auto outside = L"/Game/Levels/he/he_02/he_02.he_02";
    assert(can_prepare_arrival_under_cover(outside, true, true));
    assert(can_prepare_arrival_under_cover(L"/Game/Levels/he/he_03/he_03.he_03", true, true));
    assert(can_prepare_arrival_under_cover(L"/Game/Levels/ts/ts_01/ts_01.ts_01", true, true));
    assert(can_prepare_arrival_under_cover(L"/Game/Levels/mi/mi_01/mi_01.mi_01", true, true));
    assert(can_prepare_arrival_under_cover(L"/Game/Levels/kg/kg_02/kg_02.kg_02", true, true));
    assert(can_prepare_arrival_under_cover(L"/Game/Maps/GardenOfAssemblage/umap/GoA_01.GoA_01", true, true));
    assert(!can_prepare_arrival_under_cover(outside, false, true));
    assert(!can_prepare_arrival_under_cover(outside, true, false));
    constexpr auto caribbean = L"/Game/Levels/ca/ca_01/ca_01.ca_01";
    assert(can_prepare_arrival_under_cover(caribbean, true, true));
    assert(can_prepare_arrival_under_cover(L"/Game/Levels/ca/ca_02/ca_02.ca_02", true, true));
    assert(can_prepare_arrival_under_cover(L"/Game/Levels/ca/ca_03/ca_03.ca_03", true, true));
    assert(!can_prepare_arrival_under_cover(caribbean, false, true));
    assert(!can_prepare_arrival_under_cover(caribbean, true, false));
    assert(!can_prepare_arrival_under_cover(L"/Game/Levels/kg_DLC/kg_05/kg_05.kg_05", true, true));
    assert(!can_prepare_arrival_under_cover(L"/Game/Levels/wm/wm_01/wm_01.wm_01", true, true));
    assert(!can_prepare_arrival_under_cover(L"/Game/Levels/gm/gm_01/gm_01.gm_01", true, true));
    assert(!can_prepare_arrival_under_cover(L"/Game/Cutscene/Localization/ja/he/cutscene_he03_he107", true, true));
    assert(!can_prepare_arrival_under_cover(L"/Game/Maps/Title.Title", true, true));
    assert(!can_prepare_arrival_under_cover(L"", true, true));
    using kh3ap::character_swap::travel_adoption_is_current;
    assert(travel_adoption_is_current(true, true, 16, 16));
    assert(!travel_adoption_is_current(false, true, 16, 16));
    assert(!travel_adoption_is_current(true, false, 16, 16));
    assert(!travel_adoption_is_current(true, true, 0, 16));
    assert(!travel_adoption_is_current(true, true, -1, -1));
    using kh3ap::character_swap::travel_target_after_swap;
    using kh3ap::character_swap::is_scream_strike_exit_cinematic;
    constexpr auto door_vault = L"/Game/Levels/mi/mi_04/mi_04.mi_04";
    constexpr auto scream_exit = L"/Game/Cutscene/Localization/ja/mi/cutscene_mi04_mi358."
                                 L"cutscene_mi04_mi358:PersistentLevel.mi358";
    assert(is_scream_strike_exit_cinematic(door_vault, scream_exit));
    assert(!is_scream_strike_exit_cinematic(outside, scream_exit));
    assert(!is_scream_strike_exit_cinematic(door_vault, L"mi358"));
    assert(!is_scream_strike_exit_cinematic(door_vault,
        L"/Game/Cutscene/Localization/ja/mi/cutscene_mi04_mi357."
        L"cutscene_mi04_mi357:PersistentLevel.mi357"));
    assert(!is_scream_strike_exit_cinematic(door_vault,
        L"/Game/Cutscene/Localization/ja/mi/cutscene_mi04_mi358."
        L"cutscene_mi04_mi358:PersistentLevel.cutscene_mi04_mi358_C_0"));
    using kh3ap::character_swap::is_remind_maze_exit_cinematic;
    constexpr auto remind_skein = L"/Game/Levels/kg_DLC/kg_05/kg_05.kg_05";
    constexpr auto area_d_exit =
        L"/Game/Levels/kg/kg_02/umap/kg_02_camera_ms081.kg_02_camera_ms081:PersistentLevel.ms081_C-D";
    constexpr auto area_e_exit =
        L"/Game/Levels/kg/kg_02/umap/kg_02_camera_ms081.kg_02_camera_ms081:PersistentLevel.ms081_C-E";
    constexpr auto saix_exit =
        L"/Game/Levels/kg/kg_02/umap/kg_02_camera_ms091.kg_02_camera_ms091:PersistentLevel.ms091_D-E";
    assert(is_remind_maze_exit_cinematic(remind_skein, area_d_exit));
    assert(is_remind_maze_exit_cinematic(remind_skein, saix_exit));
    assert(!is_remind_maze_exit_cinematic(remind_skein, area_e_exit));
    assert(!is_remind_maze_exit_cinematic(L"/Game/Levels/kg/kg_02/kg_02.kg_02", area_d_exit));
    assert(!is_remind_maze_exit_cinematic(L"/Game/Levels/kg/kg_02/kg_02.kg_02", saix_exit));
    assert(!is_remind_maze_exit_cinematic(remind_skein, L"ms081_C-D"));
    assert(!is_remind_maze_exit_cinematic(remind_skein, L"ms091_D-E"));
    using kh3ap::character_swap::collapse_sora_role_is_inactive;
    constexpr auto thebes = L"/Game/Levels/he/he_03/he_03.he_03";
    constexpr auto collapse_one = L"/Game/Levels/he/he_03/umap/he_03_camera_ms024s."
                                 L"he_03_camera_ms024s:PersistentLevel.ms024s_01";
    constexpr auto collapse_two = L"/Game/Levels/he/he_03/umap/he_03_camera_ms024s."
                                 L"he_03_camera_ms024s:PersistentLevel.ms024s_02";
    assert(collapse_sora_role_is_inactive(thebes, collapse_one, 2, 2));
    assert(collapse_sora_role_is_inactive(thebes, collapse_two, 1, 1));
    assert(!collapse_sora_role_is_inactive(thebes, collapse_one, 2, 1));
    assert(!collapse_sora_role_is_inactive(thebes, collapse_one, 2, 0));
    assert(!collapse_sora_role_is_inactive(thebes, collapse_one, 0, 0));
    assert(!collapse_sora_role_is_inactive(thebes, collapse_one, 1, 2));
    assert(!collapse_sora_role_is_inactive(thebes, L"ms024s_01", 2, 2));
    assert(!collapse_sora_role_is_inactive(remind_skein, collapse_one, 2, 2));
    assert(!collapse_sora_role_is_inactive(thebes,
        L"/Game/Cutscene/Localization/ja/he/cutscene_he03_he107.cutscene_he03_he107:PersistentLevel.he107", 2, 2));
    int intent = travel_target_after_swap(-1, 16, 0, false);
    assert(intent == 16);
    // A native cinematic Sora handoff must not undo the player's choice.
    intent = travel_target_after_swap(intent, 2, 2, true);
    assert(intent == 16);
    intent = travel_target_after_swap(intent, 16, 2, true);
    assert(intent == 16);
    // An explicit return button does cancel automatic restoration.
    intent = travel_target_after_swap(intent, 2, 2, false);
    assert(intent == -1);
    assert(travel_target_after_swap(-1, 2, 2, true) == -1);
    assert(travel_target_after_swap(16, 17, 0, false) == 17);
    assert(travel_target_after_swap(-1, 10, 3, false) == 10);
}
