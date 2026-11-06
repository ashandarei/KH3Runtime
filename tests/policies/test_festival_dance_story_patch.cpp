#include "kh3ap/features/story/world_fixes/festival_dance_story_patch.h"

#include <cassert>

int main()
{
    using kh3ap::RuntimeFestivalDanceStoryObjectKind;

    static_assert(
            kh3ap::runtime_festival_dance_story_slot_option ==
            "fast_dance_minigame");
    static_assert(kh3ap::runtime_festival_dance_story_property_specs.size() == 3);
    static_assert(kh3ap::runtime_festival_dance_story_property_specs[0].property_name ==
                  L"Wave1ClearValue");
    static_assert(kh3ap::runtime_festival_dance_story_property_specs[0].vanilla_value == 500);
    static_assert(kh3ap::runtime_festival_dance_story_property_specs[0].fast_value == 40);
    static_assert(kh3ap::runtime_festival_dance_story_property_specs[1].property_name ==
                  L"WaveRaDanceFinishValue");
    static_assert(kh3ap::runtime_festival_dance_story_property_specs[1].vanilla_value == 18);
    static_assert(kh3ap::runtime_festival_dance_story_property_specs[1].fast_value == 2);
    static_assert(kh3ap::runtime_festival_dance_story_property_specs[2].property_name ==
                  L"MissionClearValue");
    static_assert(kh3ap::runtime_festival_dance_story_property_specs[2].vanilla_value == 2000);
    static_assert(kh3ap::runtime_festival_dance_story_property_specs[2].fast_value == 540);
    static_assert(kh3ap::runtime_festival_dance_story_class_name ==
                  L"ra_02_mission_Dance_C");
    static_assert(kh3ap::runtime_festival_dance_story_manager_class_name ==
                  L"lv_ra_FestivalDance_C");
    static_assert(kh3ap::runtime_festival_dance_story_timing_property_specs.size() == 2);
    static_assert(kh3ap::runtime_festival_dance_story_timing_property_specs[0].property_name ==
                  L"RapunzelDanceTotalTime");
    static_assert(kh3ap::runtime_festival_dance_story_timing_property_specs[0].vanilla_value ==
                  28.0F);
    static_assert(kh3ap::runtime_festival_dance_story_timing_property_specs[0].fast_value ==
                  4.0F);
    static_assert(kh3ap::runtime_festival_dance_story_timing_property_specs[1].property_name ==
                  L"FlynnSplineStayTime");
    static_assert(kh3ap::runtime_festival_dance_story_timing_property_specs[1].vanilla_value ==
                  10.0F);
    static_assert(kh3ap::runtime_festival_dance_story_timing_property_specs[1].fast_value ==
                  2.0F);

    constexpr auto default_object_kind =
            kh3ap::classify_runtime_festival_dance_story_object(
                    L"ra_02_mission_Dance_C",
                    L"ra_02_mission_Dance_C /Game/Levels/ra/ra_02/umap/"
                    L"ra_02_mission_Dance.Default__ra_02_mission_Dance_C");
    static_assert(default_object_kind ==
                  RuntimeFestivalDanceStoryObjectKind::ClassDefaultObject);

    constexpr auto live_object_kind =
            kh3ap::classify_runtime_festival_dance_story_object(
                    L"ra_02_mission_Dance_C",
                    L"ra_02_mission_Dance_C /Game/Levels/ra/ra_02/umap/"
                    L"ra_02_mission_Dance.ra_02_mission_Dance:PersistentLevel."
                    L"ra_02_mission_Dance_C_1");
    static_assert(live_object_kind == RuntimeFestivalDanceStoryObjectKind::LiveInstance);

    static_assert(kh3ap::classify_runtime_festival_dance_story_object(
                          L"lv_ra_FestivalDance_C",
                          L"lv_ra_FestivalDance_C /Game/Levels/ra/ra_02/umap/"
                          L"ra_02_mission_Dance.ra_02_mission_Dance:PersistentLevel."
                          L"lv_ra_FestivalDance_C_0") ==
                  RuntimeFestivalDanceStoryObjectKind::NotTarget);
    static_assert(kh3ap::is_runtime_festival_dance_story_manager_live_object(
            L"lv_ra_FestivalDance_C",
            L"lv_ra_FestivalDance_C /Game/Levels/ra/ra_02/umap/"
            L"ra_02_mission_Dance.ra_02_mission_Dance:PersistentLevel."
            L"lv_ra_FestivalDance"));
    static_assert(!kh3ap::is_runtime_festival_dance_story_manager_live_object(
            L"lv_ra_FestivalDance_C",
            L"lv_ra_FestivalDance_C /Game/Blueprints/Level/ra/ra_mission_dance/"
            L"lv_ra_FestivalDance.Default__lv_ra_FestivalDance_C"));
    static_assert(!kh3ap::is_runtime_festival_dance_story_manager_live_object(
            L"lv_ra_FestivalDance_C",
            L"lv_ra_FestivalDance_C /Game/Levels/ra/ra_02/umap/"
            L"ra_02_mission_Dance_After.ra_02_mission_Dance_After:PersistentLevel."
            L"lv_ra_FestivalDance_Minigame"));
    static_assert(kh3ap::classify_runtime_festival_dance_story_object(
                          L"ra_02_mission_Dance_After_C",
                          L"ra_02_mission_Dance_After_C /Game/Levels/ra/ra_02/umap/"
                          L"ra_02_mission_Dance_After.ra_02_mission_Dance_After:PersistentLevel."
                          L"ra_02_mission_Dance_After_C_0") ==
                  RuntimeFestivalDanceStoryObjectKind::NotTarget);
    static_assert(kh3ap::classify_runtime_festival_dance_story_object(
                          L"ra_02_mission_Dance_C",
                          L"ra_02_mission_Dance_C /Game/Levels/ra/ra_01/umap/"
                          L"ra_01_mission_Dance.ra_01_mission_Dance:PersistentLevel."
                          L"ra_02_mission_Dance_C_0") ==
                  RuntimeFestivalDanceStoryObjectKind::NotTarget);

    constexpr auto completed_progress = [] {
        kh3ap::RuntimeFestivalDanceStoryPatchProgress progress{};
        progress.record_success(RuntimeFestivalDanceStoryObjectKind::ClassDefaultObject);
        progress.record_success(RuntimeFestivalDanceStoryObjectKind::LiveInstance);
        progress.record_manager_timing_success();
        return progress;
    }();
    static_assert(completed_progress.contains(
            RuntimeFestivalDanceStoryObjectKind::ClassDefaultObject));
    static_assert(completed_progress.contains(
            RuntimeFestivalDanceStoryObjectKind::LiveInstance));
    static_assert(completed_progress.manager_timing_complete());
    static_assert(completed_progress.complete());

    kh3ap::RuntimeFestivalDanceStoryPatchProgress incomplete_progress{};
    incomplete_progress.record_success(RuntimeFestivalDanceStoryObjectKind::ClassDefaultObject);
    incomplete_progress.record_success(RuntimeFestivalDanceStoryObjectKind::LiveInstance);
    assert(!incomplete_progress.complete());
    return 0;
}
