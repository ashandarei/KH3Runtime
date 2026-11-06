#include "kh3ap/features/world/garden_return/garden_return_policy.h"
#include <cassert>

int main()
{
    using kh3ap::camp_return_world_allowed;
    using kh3ap::camp_return_corona_landing_allowed;
    using kh3ap::camp_return_requires_corona_landing;

    constexpr auto corona = L"World /Game/Levels/ra/ra_01/ra_01.ra_01";
    constexpr auto kingdom = L"World /Game/Levels/ra/ra_02/ra_02.ra_02";
    constexpr auto garden = L"World /Game/Maps/GardenOfAssemblage/umap/GoA_01.GoA_01";
    constexpr auto olympus = L"World /Game/Levels/he/he_02/he_02.he_02";
    assert(camp_return_requires_corona_landing(corona));
    assert(camp_return_requires_corona_landing(kingdom));
    assert(!camp_return_corona_landing_allowed(corona, false));
    assert(!camp_return_corona_landing_allowed(kingdom, false));
    assert(camp_return_corona_landing_allowed(corona, true));
    assert(camp_return_corona_landing_allowed(kingdom, true));
    assert(camp_return_corona_landing_allowed(garden, false));
    assert(camp_return_corona_landing_allowed(olympus, false));
    assert(!camp_return_requires_corona_landing(L"World /Game/Maps/APKH02CastleTown/APKH02CastleTown.APKH02CastleTown"));
    // Prepare & Retry must not offer an escape from the opening encounter.
    assert(!camp_return_world_allowed(L"World /Game/Levels/ew_DLC/ew_04/ew_04.ew_04"));
    // Keep normal world returns and Garden's Return to Space available.
    assert(camp_return_world_allowed(L"World /Game/Levels/he/he_02/he_02.he_02"));
    assert(camp_return_world_allowed(L"World /Game/Levels/ew/ew_01/ew_01.ew_01"));
    assert(camp_return_world_allowed(L"World /Game/Maps/GardenOfAssemblage/umap/GoA_01.GoA_01"));
    assert(camp_return_world_allowed(L"World /Game/Maps/APKH02CastleTown/APKH02CastleTown.APKH02CastleTown"));
    assert(camp_return_world_allowed(L"World /Game/Maps/APKH02WorldWithin/APKH02WorldWithin.APKH02WorldWithin"));
    assert(!camp_return_world_allowed(L"World /Game/Maps/APKH02CastleTown/Other.Other"));
    assert(!camp_return_world_allowed(L"World /Game/Maps/StationOfAwakening/umap/StationOfAwakening.StationOfAwakening"));
    assert(!camp_return_world_allowed(L"World /Game/Maps/TresGameTitle.TresGameTitle"));
    assert(!camp_return_world_allowed(L""));
}
