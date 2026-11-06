#include "kh3ap/features/colors/color_timing.h"
#include <cassert>
#include <iostream>

int main()
{
    using namespace kh3ap;
    assert(runtime_color_safety_ready(true, L"ready"));
    assert(runtime_color_safety_ready(false, L"player_pawn_not_stable"));
    for (auto reason : {L"blocking_ui_active", L"shotlock_active", L"no_player_pawn",
                       L"player_not_live", L"no_live_player_pawn", L"unsafe_runtime_map", L"unknown"})
        assert(!runtime_color_safety_ready(false, reason));
    for (auto name : {L"Pawn /Game/Maps/StationOfAwakening/umap/StationOfAwakening.StationOfAwakening:PersistentLevel.Sora",
                     L"Pawn /Game/bt_51.bt_51:PersistentLevel.Sora",
                     L"Pawn /Game/TresGameTitle.TresGameTitle:PersistentLevel.Sora",
                     L"Pawn /Game/Sora.Default__Sora", L""})
        assert(!runtime_color_map_allowed(name));
    assert(runtime_color_map_allowed(L"Pawn /Game/GoA_01.GoA_01:PersistentLevel.Sora"));
    assert(runtime_color_map_allowed(L"Pawn /Game/ts_01.ts_01:PersistentLevel.Woody"));
    std::cout << "Color safety gate regression tests passed\n";
}
