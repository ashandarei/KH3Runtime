#include "kh3ap/features/story/minigames/beat_of_lead_completion.h"

#include <cassert>

int main()
{
    using kh3ap::RuntimeBeatOfLeadCompletionAction;
    using kh3ap::RuntimeBeatOfLeadCompletionTracker;
    using kh3ap::RuntimeBeatOfLeadObjectKind;

    static_assert(
            kh3ap::runtime_beat_of_lead_slot_option ==
            "fast_beat_of_lead");
    static_assert(
            kh3ap::runtime_beat_of_lead_retry_interval ==
            std::chrono::milliseconds(250));
    static_assert(
            kh3ap::runtime_beat_of_lead_minigame_property_name ==
            L"Minigame");
    static_assert(
            kh3ap::runtime_beat_of_lead_score_property_name == L"SumScore");
    static_assert(
            kh3ap::runtime_beat_of_lead_level_script_class_name ==
            L"ts_03_gameflow_C");
    static_assert(
            kh3ap::runtime_beat_of_lead_wipe_out_function_name ==
            L"OnWipeOut");
    static_assert(
            kh3ap::runtime_beat_of_lead_story_wipe_out_group == L"hyper");

    static_assert(
            kh3ap::classify_runtime_beat_of_lead_object(
                    L"g_ts_GigasGameMgr_C",
                    L"g_ts_GigasGameMgr_C /Game/Blueprints/Gimmick/ts/"
                    L"g_ts_GigasGameMgr.Default__g_ts_GigasGameMgr_C") ==
            RuntimeBeatOfLeadObjectKind::ClassDefaultObject);
    static_assert(
            kh3ap::classify_runtime_beat_of_lead_object(
                    L"g_ts_GigasGameMgr_C",
                    L"g_ts_GigasGameMgr_C /Game/Levels/ts/ts_03/umap/"
                    L"ts_03_GigasGame_Mission.ts_03_GigasGame_Mission:PersistentLevel."
                    L"g_ts_GigasGameMgr_C_0") ==
            RuntimeBeatOfLeadObjectKind::LiveInstance);
    static_assert(
            kh3ap::classify_runtime_beat_of_lead_object(
                    L"g_ts_GigasGameMgr_Debug_C",
                    L"g_ts_GigasGameMgr_Debug_C /Game/Levels/ts/ts_03/umap/"
                    L"ts_03_GigasGame.ts_03_GigasGame:PersistentLevel."
                    L"g_ts_GigasGameMgr_C_0") ==
            RuntimeBeatOfLeadObjectKind::NotTarget);
    static_assert(
            kh3ap::classify_runtime_beat_of_lead_object(
                    L"g_ts_GigasGameMgr_C",
                    L"g_ts_GigasGameMgr_C /Game/Levels/ts/ts_02/umap/"
                    L"ts_02_Gimmick.ts_02_Gimmick:PersistentLevel."
                    L"g_ts_GigasGameMgr_C_0") ==
            RuntimeBeatOfLeadObjectKind::NotTarget);

    RuntimeBeatOfLeadCompletionTracker tracker{};
    assert(
            tracker.observe_state(0, false, 5000) ==
            RuntimeBeatOfLeadCompletionAction::Ignored);
    assert(
            tracker.observe_state(0x1000, false, 0) ==
            RuntimeBeatOfLeadCompletionAction::Waiting);
    assert(
            tracker.observe_state(0x1000, true, 5000) ==
            RuntimeBeatOfLeadCompletionAction::Waiting);
    assert(!tracker.completion_dispatched);
    assert(
            tracker.observe_state(0x1000, false, 5000) ==
            RuntimeBeatOfLeadCompletionAction::Complete);
    assert(tracker.completion_dispatched);
    assert(
            tracker.observe_state(0x1000, false, 10000) ==
            RuntimeBeatOfLeadCompletionAction::Ignored);

    tracker.rearm_after_failed_completion();
    assert(!tracker.completion_dispatched);
    assert(
            tracker.observe_state(0x1000, false, 5000) ==
            RuntimeBeatOfLeadCompletionAction::Complete);

    assert(
            tracker.observe_state(0x2000, true, 5000) ==
            RuntimeBeatOfLeadCompletionAction::Waiting);
    assert(tracker.manager_identity == 0x2000);
    assert(!tracker.completion_dispatched);

    tracker.reset();
    assert(tracker.manager_identity == 0);
    assert(!tracker.completion_dispatched);
    return 0;
}
