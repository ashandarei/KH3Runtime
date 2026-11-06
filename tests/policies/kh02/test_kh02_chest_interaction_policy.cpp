#include "kh3ap/features/kh02/treasure/kh02_chest_interaction_policy.h"
#include "kh3ap/features/kh02/treasure/kh02_postclear_treasure_policy.h"
#include <cassert>

int main()
{
    using namespace kh3ap::kh02_chest_interaction;
    assert(accepts_player_state(idle_state));
    assert(accepts_player_state(run_state));
    constexpr auto castle = L"/Game/Maps/APKH02CastleTown/APKH02CastleTown.APKH02CastleTown";
    constexpr auto within = L"/Game/Maps/APKH02WorldWithin/APKH02WorldWithin.APKH02WorldWithin";
    for (const auto* host : {castle, within})
    {
        assert(kh3ap::kh02_postclear_treasure_package(host, false).empty());
        assert(!kh3ap::kh02_postclear_treasure_package(host, true).empty());
    }
    for (const auto* other : {L"/Game/Levels/dw/dw_01/dw_01.dw_01", L"/Game/Maps/GardenOfAssemblage/umap/GoA_01.GoA_01",
                              L"/Game/Maps/APKH02Mirrors/APKH02Forest.APKH02Forest"})
    {
        assert(kh3ap::kh02_postclear_treasure_package(other, true).empty());
    }
    // Never preempt an attack, airborne movement, damage, death or a scene.
    for (int state = 0; state <= 255; ++state)
    {
        if (state != idle_state && state != run_state)
        {
            assert(!accepts_player_state(static_cast<uint8_t>(state)));
        }
    }
}
