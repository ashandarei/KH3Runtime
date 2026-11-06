#include "kh3ap/features/kh02/world/kh02_castle_scenery_plan.h"
#include "kh3ap/features/kh02/world/kh02_castle_boss_streams.h"
#include "kh3ap/features/kh02/progress/kh02_checkpoint_rules.h"
#include <cassert>
#include <map>
#include <string>

int main()
{
    using namespace kh3ap;
    std::map<std::wstring, int> flags;
    const auto read = [&](const wchar_t* key)
    {
        const auto found = flags.find(key);
        return found == flags.end() ? 0 : found->second;
    };
    assert(kh02_castle_scenery_plan(kh02_checkpoint::story_end, false, read).empty());
    assert(kh02_castle_scenery_plan(kh02_checkpoint::gear_scene_dw011, true, read).empty());
    assert(kh02_castle_scenery_plan(kh02_checkpoint::gear_scene_dw012, true, read) ==
           std::vector<std::wstring_view>{L"dw02_CS_12_End"});
    const std::pair<int, std::wstring_view> gears[]{{kh02_checkpoint::first_gear, L"dw02_GEAR_Get1"},
                                                    {kh02_checkpoint::second_gear, L"dw02_GEAR_Get2"},
                                                    {kh02_checkpoint::third_gear, L"dw02_GEAR_Get3"},
                                                    {kh02_checkpoint::fourth_gear, L"dw02_GEAR_Get4"}};
    for (const auto& [checkpoint, command] : gears)
    {
        const auto plan = kh02_castle_scenery_plan(checkpoint, true, read);
        assert(plan.size() == 1 && plan.front() == command);
    }
    flags[L"gameflow_DW_TUTORIAL_12_END"] = 1;
    const auto before = flags;
    const auto one_unlock = kh02_castle_scenery_plan(kh02_checkpoint::first_gear, true, read);
    assert((one_unlock == std::vector<std::wstring_view>{L"dw02_GEAR_Get1", L"dw02_Get_Flight"}));
    assert(flags == before);
    flags.clear();
    const auto revisit = kh02_castle_scenery_plan(kh02_checkpoint::gear_scene_dw014, true, read);
    assert((revisit == std::vector<std::wstring_view>{L"dw02_Revisit", L"dw02_Get_Flight", L"dw02_Get_Shoot",
                                                      L"dw02_Get_Slide", L"dw02_Get_Style"}));
    assert(flags.empty());

    // A saved final-gear checkpoint returns to the field with its original
    // trigger armed, without advancing to the boss Retry checkpoint. Exercise
    // repeated portal/native-save restoration with the same persisted bank.
    kh02_progress::Flags saved{{"gameflow_DW", kh02_checkpoint::fourth_gear},
                               {"gameflow_DW_GEAR_GETCOUNT", kh02_gear_counter::all_collected},
                               {"gameflow_DW_VISITED_2", 1}};
    for (int visit = 0; visit < 3; ++visit)
    {
        const auto restored = kh02_progress::enter_flags(saved, kh02_checkpoint::gear_scene_dw011,
                                                         kh02_progress::resume_castle_entry(saved));
        assert(restored == saved);
        const int checkpoint = kh02_progress::flag_value(restored, "gameflow_DW");
        assert(checkpoint != kh02_checkpoint::castle_tower_start);
        assert(kh02_castle_boss_streams_required(checkpoint,
                                                 kh02_progress::flag_value(restored, "gameflow_DW_GEAR_GETCOUNT")));
        saved = restored;
    }
    assert(kh02_castle_boss_streams_required(kh02_checkpoint::castle_tower_start, 40));
    for (const int checkpoint :
         {kh02_checkpoint::gear_scene_dw011, kh02_checkpoint::third_gear, kh02_checkpoint::castle_tower_end,
          kh02_checkpoint::gear_scene_dw013, kh02_checkpoint::story_end})
    {
        assert(!kh02_castle_boss_streams_required(checkpoint, 40));
    }
    for (const int count : {0, 10, 20, 30})
    {
        assert(!kh02_castle_boss_streams_required(kh02_checkpoint::fourth_gear, count));
    }
}
