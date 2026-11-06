#include "kh3ap/features/kh02/combat/kh02_enemy_scaling_policy.h"
#include "kh3ap/features/kh02/treasure/kh02_hud_treasure_policy.h"
#include "kh3ap/features/kh02/ui/kh02_mission_counter_policy.h"
#include <cassert>
#include <limits>
#include <string>

int main()
{
    using namespace kh3ap;
    assert(kh02_scaled_enemy_hp(100, 2.f) == 200);
    assert(kh02_scaled_enemy_hp(10, .25f) == 2);
    assert(kh02_scaled_enemy_hp(100, 0.f) == 1);
    assert(kh02_scaled_enemy_hp(100'000, 10.f) == 1'000'000);
    assert(kh02_scaled_enemy_hp(100, 99.f, 500) == 500);
    for (const auto rate : {std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(), 10'001.f})
    {
        bool rejected = false;
        try
        {
            kh02_scaled_enemy_hp(100, rate);
        }
        catch (const std::runtime_error& error)
        {
            rejected = std::string(error.what()) == "kh02_stats_scaled_hp_range";
        }
        assert(rejected);
    }
    assert(kh02_imported_treasure_area(L"DW_BOX026") == kh02_depths_treasure_area);
    assert(kh02_imported_treasure_area(L"DW_BOX030") == kh02_depths_treasure_area);
    assert(kh02_imported_treasure_area(L"DW_LBOX04") == kh02_depths_treasure_area);
    for (const auto* chest : {L"DW_BOX002", L"DW_BOX005", L"DW_BOX031", L"DW_BOX036"})
    {
        assert(kh02_imported_treasure_area(chest) == kh02_castle_treasure_area);
    }
    assert(kh02_imported_treasure_area(L"DW_BOX025") == kh02_mirror_treasure_area);
    assert(kh02_imported_treasure_area(L"DW_BOX033") == kh02_mirror_treasure_area);
    assert(kh02_mission_counter_valid(0, 1));
    assert(kh02_mission_counter_valid(99, 99));
    assert(!kh02_mission_counter_valid(-1, 5));
    assert(!kh02_mission_counter_valid(1, 0));
    assert(!kh02_mission_counter_valid(0, 100));
    assert(!kh02_mission_counter_valid(6, 5));

    Kh02MissionCounterPresentation counter;
    counter.request(true);
    assert(counter.requested && counter.dirty && !counter.visible);
    counter.presented();
    assert(counter.visible && !counter.dirty);
    // Losing possession temporarily closes the UI, but not the source request.
    counter.suspend();
    assert(counter.requested && counter.dirty && !counter.visible);
    counter.suspend();
    assert(counter.dirty);
    counter.presented();
    assert(counter.visible && !counter.dirty);
    // An explicit source close must remain closed through later swaps.
    counter.request(false);
    counter.suspend();
    assert(!counter.requested && !counter.visible && !counter.dirty);
    counter.suspend();
    assert(!counter.dirty);
    counter.request(true);
    counter.suspend();
    assert(counter.dirty);
    // A new world discards the prior source's request.
    counter = {};
    assert(!counter.requested && !counter.visible && !counter.dirty);
}
