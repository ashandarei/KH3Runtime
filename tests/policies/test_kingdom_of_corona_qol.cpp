#include "kh3ap/features/story/world_fixes/kingdom_of_corona_qol.h"

#include <cassert>

using namespace kh3ap;

int main()
{
    static_assert(runtime_corona_story_transitions.size() == 2);
    assert(runtime_corona_rabbit_passage_gate_flag ==
           L"gameflow_RA_Rabbit");
    assert(runtime_corona_rabbit_passage_activation_label ==
           L"RA_RABBIT_FINISH");
    assert(runtime_corona_rabbit_passage_gate_frame ==
           L"Function /Game/Maps/ra/umap/ra_01/"
           L"ra_01_AreaC_A.ra_01_AreaC_A_C:"
           L"ExecuteUbergraph_ra_01_AreaC_A");
    assert(is_runtime_corona_rabbit_passage_gate_callsite(
            runtime_corona_rabbit_passage_gate_frame,
            L""));
    assert(is_runtime_corona_rabbit_passage_gate_callsite(
            L"Function /Script/TresGame.TresGameBlueprintLibrary:"
            L"GreaterEqual_FlagLabel",
            L"ra_01_AreaC_A_C /Game/Maps/ra/umap/ra_01/"
            L"ra_01_AreaC_A.ra_01_AreaC_A:PersistentLevel."
            L"ra_01_AreaC_A_C_0 @123 real=true"));
    assert(!is_runtime_corona_rabbit_passage_gate_callsite(
            L"Function /Game/Levels/ra/ra_01/umap/"
            L"ra_01_gimmick_AreaC.ra_01_gimmick_AreaC_C:"
            L"ExecuteUbergraph_ra_01_gimmick_AreaC",
            L""));
    assert(!is_runtime_corona_rabbit_passage_gate_callsite(
            L"Function /Game/Maps/ra/umap/ra_01/"
            L"ra_01_AreaC_A.ra_01_AreaC_A_C:"
            L"ExecuteUbergraph_ra_01_AreaC_A_Copy",
            L"ra_01_AreaC_A_C /Game/Maps/ra/umap/ra_01/"
            L"ra_01_AreaC_A.ra_01_AreaC_A:PersistentLevel."
            L"ra_01_AreaC_A_C_Copy_0 @123 real=true"));
    assert(!is_runtime_corona_rabbit_passage_gate_context(
            L"ra_01_AreaC_A_C /Game/Maps/ra/umap/ra_01/"
            L"ra_01_AreaC_A.ra_01_AreaC_A:PersistentLevel."
            L"ra_01_AreaC_A_C_"));
    const auto* tower = find_runtime_corona_story_transition(
            L"gameflow_RA",
            420);
    assert(tower);
    assert(tower->target_value == 500);
    assert(tower->observed_label == L"RA_TOWER_EVENT2");
    assert(tower->target_label == L"RA_FOREST_REAL10");
    assert(tower->companion_flag == L"gameflow_RA_Dandelion");
    assert(tower->companion_value == 10);
    assert(tower->companion_label == L"RA_DANDELION_START");

    const auto* battle = find_runtime_corona_story_label_transition(
            L"gameflow_RA",
            L"RA_FOREST_BATTLE21");
    assert(battle);
    assert(battle->observed_value == 2031);
    assert(battle->target_value == 2500);
    assert(battle->target_label == L"RA_FOREST_REAL30");
    assert(battle->companion_flag == L"gameflow_RA_Bird");
    assert(battle->companion_value == 10);
    assert(battle->companion_label == L"RA_BIRD_START");

    assert(!find_runtime_corona_story_transition(L"gameflow_RA", 421));
    assert(!find_runtime_corona_story_transition(L"gameflow_TT", 420));
    assert(!find_runtime_corona_story_label_transition(
            L"gameflow_RA",
            L"RA_FOREST_REAL10"));
    assert(is_runtime_corona_powerwild_camera_flag(
            L"gameflow_RA_PWildCamera1"));
    assert(!is_runtime_corona_powerwild_camera_flag(
            L"gameflow_RA_PWildCamera2"));
    assert(is_runtime_corona_event_frame(
            L"Function /Game/Levels/ra/ra_01/umap/ra_01_gameflow."
            L"ra_01_gameflow_C:ExecuteUbergraph_ra_01_gameflow"));
    assert(is_runtime_corona_event_frame(
            L"Function /Game/Levels/ra/ra_01/umap/ra_01_enemy_41."
            L"ra_01_enemy_41_C:ExecuteUbergraph_ra_01_enemy_41"));
    assert(!is_runtime_corona_event_frame(
            L"Function /Game/Levels/ra/ra_02/umap/ra_02_gameflow."
            L"ra_02_gameflow_C:ExecuteUbergraph_ra_02_gameflow"));
    assert(!is_runtime_corona_event_frame(
            L"Function /Game/Levels/he/he_02/umap/he_02_gameflow."
            L"he_02_gameflow_C:ExecuteUbergraph_he_02_gameflow"));
    return 0;
}
