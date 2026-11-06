#include "kh3ap/features/grants/battle_level/battle_level_progress.h"

#include <cassert>
#include <optional>

int main()
{
    using kh3ap::clamp_battle_level_offset;
    using kh3ap::resolve_battle_level_target;

    assert(clamp_battle_level_offset(-99) == -3);
    assert(clamp_battle_level_offset(-3) == -3);
    assert(clamp_battle_level_offset(0) == 0);
    assert(clamp_battle_level_offset(3) == 3);
    assert(clamp_battle_level_offset(99) == 10);
    assert(!resolve_battle_level_target(0, 0).has_value());
    assert(resolve_battle_level_target(1, -3).value() == 2);
    assert(resolve_battle_level_target(5, -3).value() == 2);
    assert(resolve_battle_level_target(6, -3).value() == 3);
    assert(resolve_battle_level_target(37, 0).value() == 37);
    assert(resolve_battle_level_target(37, 3).value() == 40);
    assert(resolve_battle_level_target(37, -2).value() == 35);
    assert(resolve_battle_level_target(100, 3).value() == 103);
    assert(resolve_battle_level_target(10, -99).value() == 7);
    assert(resolve_battle_level_target(10, 99).value() == 20);

    constexpr auto darkside = L"World /Game/Levels/ew_DLC/ew_04/ew_04.ew_04";
    for (const uint64_t sora_level : {uint64_t{0}, uint64_t{1}, uint64_t{37}, uint64_t{100}})
    {
        for (const int32_t offset : {-99, -3, 0, 10, 99})
        {
            assert(resolve_battle_level_target(sora_level, offset, darkside, true).value() == 1);
            assert(resolve_battle_level_target(sora_level, offset, darkside, false).value() == 1);
        }
    }
    assert(kh3ap::fixed_battle_level_for_map(
                   L"/Game/Levels/ew_DLC/ew_04/umap/ew_04_gameflow.ew_04_gameflow_C").value() == 1);
    assert(kh3ap::fixed_battle_level_for_map(
                   L"/game/levels/EW_dlc/EW_04/EW_04.EW_04").value() == 1);
    for (const auto other_map : {
                 L"", L"ew_04", L"/Game/Levels/ew/ew_01/ew_01",
                 L"/Game/Levels/ew_DLC/ew_05/ew_05",
                 L"/Game/Levels/ew_DLC/ew_040/ew_040",
                 L"/Game/Levels/ew_DLC/ew_04_backup/ew_04"})
    {
        assert(!kh3ap::fixed_battle_level_for_map(other_map).has_value());
        assert(resolve_battle_level_target(37, 3, other_map).value() == 40);
        assert(!resolve_battle_level_target(37, 3, other_map, false).has_value());
    }

    return 0;
}
