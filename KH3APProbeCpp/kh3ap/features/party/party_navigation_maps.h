#pragma once

#include <string_view>

namespace kh3ap
{
    struct RuntimePartyNavigationMap
    {
        std::wstring_view level;
        int group;
    };

    // Shipped EGS navigation audit and live partial-coverage reproductions.
    // Exact persistent levels only; preserve healthy Group2 and story sublevels.
    inline constexpr RuntimePartyNavigationMap runtime_party_navigation_maps[] = {
        {L"Level /Game/Levels/bt/bt_01/bt_01.bt_01:PersistentLevel", 1},
        {L"Level /Game/Levels/bt/bt_02/bt_02.bt_02:PersistentLevel", 1},
        {L"Level /Game/Levels/bt/bt_04/bt_04.bt_04:PersistentLevel", 1},
        {L"Level /Game/Levels/bx/bx_01/bx_01.bx_01:PersistentLevel", 3},
        {L"Level /Game/Levels/bx/bx_02/bx_02.bx_02:PersistentLevel", 3},
        {L"Level /Game/Levels/dw_DLC/dw_22/dw_22.dw_22:PersistentLevel", 1},
        {L"Level /Game/Levels/ew_DLC/ew_04/ew_04.ew_04:PersistentLevel", 1},
        {L"Level /Game/Levels/ex/ex_23/ex_23.ex_23:PersistentLevel", 3},
        {L"Level /Game/Levels/ex/ex_34/ex_34.ex_34:PersistentLevel", 3},
        {L"Level /Game/Levels/ex/ex_35/ex_35.ex_35:PersistentLevel", 3},
        {L"Level /Game/Levels/ex/ex_37/ex_37.ex_37:PersistentLevel", 1},
        {L"Level /Game/Levels/ex/ex_39/ex_39.ex_39:PersistentLevel", 1},
        {L"Level /Game/Levels/fz/fz_01/fz_01.fz_01:PersistentLevel", 1},
        {L"Level /Game/Levels/fz/fz_04/fz_04.fz_04:PersistentLevel", 1},
        {L"Level /Game/Levels/fz/fz_06/fz_06.fz_06:PersistentLevel", 1},
        {L"Level /Game/Levels/mi/mi_01/mi_01.mi_01:PersistentLevel", 3},
        {L"Level /Game/Levels/mi/mi_02/mi_02.mi_02:PersistentLevel", 3},
        {L"Level /Game/Levels/mi/mi_03/mi_03.mi_03:PersistentLevel", 3},
        {L"Level /Game/Levels/mi/mi_04/mi_04.mi_04:PersistentLevel", 3},
    };

    // Sulley and Baymax select Group3. Olympus has no Group3 mesh in
    // HE1/5/6 and incomplete follow coverage in HE2/3/4. The other rows
    // have zero shipped Group3 tiles. Use Group2 where it supports ordinary
    // companions, or Group1 where Group2 is also empty. Story-owned and
    // solo levels are deliberately excluded.
    inline constexpr RuntimePartyNavigationMap runtime_party_large_navigation_maps[] = {
        {L"Level /Game/Levels/bt/bt_01/bt_01.bt_01:PersistentLevel", 1},
        {L"Level /Game/Levels/bt/bt_02/bt_02.bt_02:PersistentLevel", 1},
        {L"Level /Game/Levels/ca/ca_03/ca_03.ca_03:PersistentLevel", 2},
        {L"Level /Game/Levels/dw_DLC/dw_22/dw_22.dw_22:PersistentLevel", 1},
        {L"Level /Game/Levels/ew_DLC/ew_04/ew_04.ew_04:PersistentLevel", 1},
        {L"Level /Game/Levels/ex/ex_21/ex_21.ex_21:PersistentLevel", 2},
        {L"Level /Game/Levels/ex/ex_22/ex_22.ex_22:PersistentLevel", 2},
        {L"Level /Game/Levels/ex/ex_26/ex_26.ex_26:PersistentLevel", 2},
        {L"Level /Game/Levels/ex/ex_37/ex_37.ex_37:PersistentLevel", 1},
        {L"Level /Game/Levels/ex/ex_39/ex_39.ex_39:PersistentLevel", 1},
        {L"Level /Game/Levels/fz/fz_06/fz_06.fz_06:PersistentLevel", 1},
        {L"Level /Game/Levels/he/he_01/he_01.he_01:PersistentLevel", 2},
        {L"Level /Game/Levels/he/he_02/he_02.he_02:PersistentLevel", 2},
        {L"Level /Game/Levels/he/he_03/he_03.he_03:PersistentLevel", 2},
        {L"Level /Game/Levels/he/he_04/he_04.he_04:PersistentLevel", 2},
        {L"Level /Game/Levels/he/he_05/he_05.he_05:PersistentLevel", 2},
        {L"Level /Game/Levels/he/he_06/he_06.he_06:PersistentLevel", 2},
        {L"Level /Game/Levels/ra/ra_02/ra_02.ra_02:PersistentLevel", 2},
        {L"Level /Game/Levels/ts/ts_04/ts_04.ts_04:PersistentLevel", 2},
    };
}
