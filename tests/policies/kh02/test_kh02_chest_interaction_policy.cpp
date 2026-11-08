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
    constexpr auto stairs = L"/Game/Maps/APKH02Mirrors/APKH02Stairs.APKH02Stairs";
    constexpr auto corridor = L"/Game/Maps/APKH02Mirrors/APKH02Corridor.APKH02Corridor";
    constexpr auto tunnel = L"/Game/Maps/APKH02Mirrors/APKH02Tunnel.APKH02Tunnel";
    constexpr auto forest = L"/Game/Maps/APKH02Mirrors/APKH02Forest.APKH02Forest";
    constexpr auto depths = L"/Game/Maps/APKH02Mirrors/APKH02Depths.APKH02Depths";
    for (const auto* host : {castle, within, stairs, corridor, tunnel, forest, depths})
    {
        assert(!kh3ap::kh02_postclear_treasure_package(host).empty());
    }
    for (const auto* other : {L"/Game/Levels/dw/dw_01/dw_01.dw_01", L"/Game/Maps/GardenOfAssemblage/umap/GoA_01.GoA_01",
                              L"/Game/Maps/APKH02Mirrors/APKH02Phantom.APKH02Phantom"})
    {
        assert(kh3ap::kh02_postclear_treasure_package(other).empty());
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
