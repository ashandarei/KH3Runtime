#include "kh3ap/features/story/minigames/union_cross_completion.h"

#include <cassert>

int main()
{
    using kh3ap::RuntimeUnionCrossStartAction;
    using kh3ap::RuntimeUnionCrossStartTracker;
    using kh3ap::RuntimeUnionCrossObjectKind;

    static_assert(kh3ap::runtime_union_cross_slot_option == "fast_union_cross");
    static_assert(kh3ap::runtime_union_cross_final_mode == 2);
    static_assert(
            kh3ap::runtime_union_cross_retry_interval ==
            std::chrono::milliseconds(250));
    static_assert(
            kh3ap::runtime_union_cross_started_property_name == L"Started");

    constexpr auto default_object_kind =
            kh3ap::classify_runtime_union_cross_object(
                    L"g_kg_unioncross_ride_C",
                    L"g_kg_unioncross_ride_C /Game/Blueprints/Gimmick/kg/"
                    L"g_kg_unioncross/g_kg_unioncross_ride."
                    L"Default__g_kg_unioncross_ride_C");
    static_assert(
            default_object_kind ==
            RuntimeUnionCrossObjectKind::ClassDefaultObject);

    constexpr auto live_object_kind =
            kh3ap::classify_runtime_union_cross_object(
                    L"g_kg_unioncross_ride_C",
                    L"g_kg_unioncross_ride_C /Game/Levels/kg/kg_01/umap/"
                    L"kg_01_unioncross.KG_01_UnionCross:PersistentLevel."
                    L"g_kg_unioncross_ride_44");
    static_assert(
            live_object_kind == RuntimeUnionCrossObjectKind::LiveInstance);
    static_assert(
            kh3ap::classify_runtime_union_cross_object(
                    L"g_kg_unioncross_ride_C",
                    L"g_kg_unioncross_ride_C /Game/Levels/kg/kg_01/umap/"
                    L"kg_01_unioncross.KG_01_UnionCross:PersistentLevel."
                    L"g_kg_unioncross_ride_937") ==
            RuntimeUnionCrossObjectKind::LiveInstance);
    static_assert(
            kh3ap::classify_runtime_union_cross_object(
                    L"g_kg_unioncross_ride_debug_C",
                    L"g_kg_unioncross_ride_debug_C /Game/Levels/kg/kg_01/umap/"
                    L"kg_01_unioncross.KG_01_UnionCross:PersistentLevel."
                    L"g_kg_unioncross_ride_44") ==
            RuntimeUnionCrossObjectKind::NotTarget);
    static_assert(
            kh3ap::classify_runtime_union_cross_object(
                    L"g_kg_unioncross_ride_C",
                    L"g_kg_unioncross_ride_C /Game/Levels/kg/kg_01/umap/"
                    L"kg_01_unioncross_after.kg_01_unioncross_after:PersistentLevel."
                    L"g_kg_unioncross_ride_44") ==
            RuntimeUnionCrossObjectKind::NotTarget);

    RuntimeUnionCrossStartTracker tracker{};
    assert(
            tracker.observe_started(0, true) ==
            RuntimeUnionCrossStartAction::Ignored);
    assert(
            tracker.observe_started(0x1000, false) ==
            RuntimeUnionCrossStartAction::Waiting);
    assert(!tracker.completion_dispatched);
    assert(
            tracker.observe_started(0x1000, true) ==
            RuntimeUnionCrossStartAction::Complete);
    assert(tracker.completion_dispatched);
    assert(
            tracker.observe_started(0x1000, true) ==
            RuntimeUnionCrossStartAction::Ignored);

    tracker.rearm_after_failed_completion();
    assert(!tracker.completion_dispatched);
    assert(
            tracker.observe_started(0x1000, true) ==
            RuntimeUnionCrossStartAction::Complete);

    assert(
            tracker.observe_started(0x1000, false) ==
            RuntimeUnionCrossStartAction::Waiting);
    assert(!tracker.completion_dispatched);
    assert(
            tracker.observe_started(0x1000, true) ==
            RuntimeUnionCrossStartAction::Complete);

    assert(
            tracker.observe_started(0x2000, false) ==
            RuntimeUnionCrossStartAction::Waiting);
    assert(tracker.actor_identity == 0x2000);
    assert(!tracker.completion_dispatched);

    tracker.reset();
    assert(tracker.actor_identity == 0);
    assert(!tracker.completion_dispatched);
    assert(
            tracker.observe_started(0x3000, true) ==
            RuntimeUnionCrossStartAction::Complete);
    return 0;
}
