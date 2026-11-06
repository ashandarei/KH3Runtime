#include "kh3ap/features/story/cutscenes/guardians_of_light_skip.h"
#include "kh3ap/features/story/remind/remind_completion.h"

#include <cassert>

int main()
{
    static_assert(
            kh3ap::runtime_guardians_and_mickey_walk_skip_slot_option ==
            "skip_guardians_and_mickey_walk");
    static_assert(kh3ap::runtime_guardians_of_light_skip_flag == L"gameflow_KG_DLC");
    static_assert(kh3ap::runtime_guardians_of_light_skip_map_path == L"/Game/Levels/bt_DLC/bt_07/");
    static_assert(kh3ap::runtime_guardians_of_light_skip_target == 21400);
    static_assert(kh3ap::runtime_guardians_of_light_skip_target_label == L"KG_DLC_07_BATTLE_01");

    assert(kh3ap::is_runtime_guardians_of_light_skip_map_context(
            L"World /Game/Levels/bt_DLC/bt_07/bt_07.bt_07",
            L"",
            L""));
    assert(kh3ap::is_runtime_guardians_of_light_skip_map_context(
            L"",
            L"Package /Game/Levels/bt_DLC/bt_07/bt_07",
            L""));
    assert(!kh3ap::is_runtime_guardians_of_light_skip_map_context(
            L"World /Game/Levels/kg_DLC/kg_06/kg_06.kg_06",
            L"",
            L""));
    assert(!kh3ap::is_runtime_guardians_of_light_skip_map_context(
            L"World /Game/Levels/bt_DLC/bt_08/bt_08.bt_08",
            L"",
            L""));

    kh3ap::RuntimeGuardiansOfLightSkipEntryState entry_state{};
    assert(!entry_state.should_attempt(false));
    assert(entry_state.should_attempt(true));
    assert(entry_state.should_attempt(true));
    entry_state.record_success();
    assert(!entry_state.should_attempt(true));
    assert(!entry_state.should_attempt(false));
    assert(entry_state.should_attempt(true));

    static_assert(
            kh3ap::runtime_remind_completion_bridge_key ==
            L"VBonus:VBonus_DLC_015");
    static_assert(
            kh3ap::runtime_remind_completion_location_name ==
            L"Defeat Armored Xehanort in Re:Mind");
    static_assert(
            kh3ap::runtime_remind_completion_flag == L"gameflow_BT_DLC");
    static_assert(kh3ap::runtime_remind_completion_target == 430);
    static_assert(
            kh3ap::runtime_remind_completion_target_label ==
            L"BT_DLC_08_EVENT_04");
    static_assert(kh3ap::runtime_remind_completion_needs_reconcile(220));
    static_assert(!kh3ap::runtime_remind_completion_needs_reconcile(430));
    static_assert(!kh3ap::runtime_remind_completion_needs_reconcile(9999));

    const std::vector<std::wstring> unrelated_checks{
            L"VBonus:VBonus_DLC_010",
            L"TreasureRA:RA_LBOX_001",
    };
    const std::vector<std::wstring> completed_checks{
            L"VBonus:VBonus_DLC_010",
            L"VBonus:VBonus_DLC_015",
    };
    assert(!kh3ap::has_runtime_remind_completion_check(unrelated_checks));
    assert(kh3ap::has_runtime_remind_completion_check(completed_checks));

    kh3ap::RuntimeRemindCompletionReconcileState completion_state{};
    completion_state.observe_checked_locations(unrelated_checks);
    assert(!completion_state.should_attempt());
    completion_state.observe_checked_locations(completed_checks);
    assert(completion_state.should_attempt());
    completion_state.record_success();
    assert(!completion_state.should_attempt());
    completion_state.reset_for_runtime_lifecycle();
    assert(!completion_state.should_attempt());
    completion_state.observe_checked_locations(completed_checks);
    assert(completion_state.should_attempt());
    return 0;
}
