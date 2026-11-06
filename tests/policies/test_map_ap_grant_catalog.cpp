#include "kh3ap/features/grants/delivery/map_ap_grant_catalog.h"

#include <cassert>
#include <string>

int main()
{
    // Every existing exclusion must remain effective when embedded in the
    // runtime's world/pawn context, not just for an exact package string.
    for (const auto& rule : kh3ap::s_runtime_map_ap_grant_rules)
    {
        const auto context = L"World " + std::wstring{rule.match_substring} +
                L".Map p_ex001_Pawn_C:PersistentLevel.p_ex001_Pawn_C_0";
        const auto* blocked = kh3ap::find_runtime_unsafe_ap_grant_rule(context);
        assert((blocked != nullptr) == (rule.safety == kh3ap::RuntimeMapApGrantSafety::Unsafe));
    }

    // Repeated blocked polls, followed by travel, must not latch an exclusion.
    for (int poll = 0; poll < 100; ++poll)
    {
        assert(kh3ap::find_runtime_unsafe_ap_grant_rule(
                L"World /Game/Levels/bt_DLC/bt_51/bt_51.bt_51"));
        assert(!kh3ap::find_runtime_unsafe_ap_grant_rule(
                L"World /Game/Maps/StationOfAwakening/umap/StationOfAwakening.StationOfAwakening"));
        assert(!kh3ap::find_runtime_unsafe_ap_grant_rule(
                L"World /Game/Maps/StationOfAwakening/StationOfAwakening.StationOfAwakening"));
    }
    assert(!kh3ap::find_runtime_unsafe_ap_grant_rule(
            L"World /Game/Maps/GardenOfAssemblage/umap/GoA_01.GoA_01"));
    assert(!kh3ap::find_runtime_unsafe_ap_grant_rule(
            L"World /Game/Levels/he/he_02/he_02.he_02"));
    assert(!kh3ap::find_runtime_unsafe_ap_grant_rule(
            L"World /Game/Levels/kg/kg_01/kg_01.kg_01"));
    assert(kh3ap::find_runtime_unsafe_ap_grant_rule(
            L"World /Game/Levels/kg/kg_01/kg_01_unioncross.kg_01_unioncross"));

    // The Pooh field has ordinary possessed Sora; each puzzle is a separate
    // unsafe map. Travelling between them must not retain the previous rule.
    for (int round = 0; round < 3; ++round)
    {
        const std::wstring puzzle = L"/Game/Levels/po/po_0" + std::to_wstring(round + 2) +
                L"/po_0" + std::to_wstring(round + 2);
        assert(kh3ap::find_runtime_unsafe_ap_grant_rule(L"World " + puzzle));
        const auto* field = kh3ap::find_runtime_map_ap_grant_rule(
                L"World /Game/Levels/po/po_01/po_01.po_01 "
                L"p_po001_Pawn_C:PersistentLevel.p_po001_Pawn_C_0");
        assert(field && field->safety == kh3ap::RuntimeMapApGrantSafety::Safe);
        assert(!kh3ap::find_runtime_unsafe_ap_grant_rule(field->match_substring));
        assert(kh3ap::find_runtime_unsafe_ap_grant_rule(L"World " + puzzle));
    }
}
