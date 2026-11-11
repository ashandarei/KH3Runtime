#include "kh3ap/features/enemies/bosses/master_xehanort_center_policy.h"
#include <cassert>
#include <limits>

int main()
{
    using namespace kh3ap;
    const auto target = data_master_xehanort_class;
    assert(needs_master_arena_center(L"GrimGuardianess", target));
    assert(!needs_master_arena_center(target, target));
    assert(!needs_master_arena_center(L"", target));
    assert(!needs_master_arena_center(L"GrimGuardianess", data_master_ascension_class));
    assert((translated_master_arena_center({0, 0, 0}, {0, 0, 5}) == MasterArenaCenter{0, 0, 5}));
    const MasterArenaCenter corona{6899, 49874, -794};
    assert((translated_master_arena_center(corona, {3200, 0, 1800}) ==
            MasterArenaCenter{10099, 49874, 1006}));
    assert((translated_master_arena_center(corona, {0, 0, 5}) ==
            MasterArenaCenter{6899, 49874, -789}));
    assert((translated_master_arena_center(corona, {0, 0, 10}) ==
            MasterArenaCenter{6899, 49874, -784}));
    assert(
        !translated_master_arena_center({0, std::numeric_limits<float>::infinity(), 0}, {0, 0, 5}));
    assert(
        !translated_master_arena_center(corona, {0, 0, std::numeric_limits<float>::quiet_NaN()}));
    assert(!translated_master_arena_center({10000000, 0, 0}, {1, 0, 5}));
    assert(!translated_master_arena_center({0, 0, std::numeric_limits<float>::max()}, {0, 0, 10}));

    const MasterArenaCenter graveyard{3500, 40500, 1};
    const MasterArenaCenter outside_wall{6700, 40500, 1801};
    assert((constrain_master_warp_xy(graveyard, outside_wall, 1350) ==
            MasterArenaCenter{4850, 40500, 1801}));
    assert((constrain_master_warp_xy(graveyard, outside_wall, 0) ==
            MasterArenaCenter{3500, 40500, 1801}));
    const MasterArenaCenter inside_wall{4300, 41100, 1801};
    assert(constrain_master_warp_xy(graveyard, inside_wall, 1350) == inside_wall);
    assert((constrain_master_warp_xy(graveyard, inside_wall, 500) ==
            MasterArenaCenter{3900, 40800, 1801}));
    assert(!constrain_master_warp_xy(graveyard, outside_wall, -1));
    assert(!constrain_master_warp_xy(graveyard, outside_wall,
                                     std::numeric_limits<float>::quiet_NaN()));
}
