#include "kh3ap/features/kh02/world/kh02_doors.h"
#include <cassert>
#include <string>

int main()
{
    using namespace kh3ap::kh02_native_doors;
    const auto forest = L"/Game/Zevels/dw/dw_08/umap/dw_08_gameflow.dw_08_gameflow:PersistentLevel.";
    const auto depths = L"/Game/Zevels/dw/dw_09/umap/dw_09_gameflow.dw_09_gameflow:PersistentLevel.";
    const auto* before = find(forest, L"TresMapJump_dw025a_Before");
    const auto* after = find(forest, L"TresMapJump_dw025a_After");
    const auto* ending = find(depths, L"TresMapJumpDirectionalTriggerBox_1");
    assert(before && after && ending);
    assert(before->destination_map == L"/Game/Maps/APKH02Mirrors/APKH02Transition");
    assert(before->target_tag == L"dw_59_Lv_Start_01" && before->area == 8);
    assert(after->destination_map == L"/Game/Maps/APKH02Mirrors/APKH02Depths");
    assert(after->target_tag == L"dw_09_Lv_Start_01" && after->area == 8);
    assert(ending->destination_map == L"/Game/Maps/APKH02Mirrors/APKH02DestinyIslands");
    assert(ending->target_tag == L"dw_10_Lv_Start_01" && ending->area == 9);
    // A matching short actor name must never redirect another map's door.
    assert(!find(depths, L"TresMapJump_dw025a_Before"));
    assert(!find(forest, L"TresMapJumpDirectionalTriggerBox_1"));
    assert(!find(L"/Game/Zevels/dw/dw_08/umap/dw_08_gameflow_extra.Actor", L"TresMapJump_dw025a_After"));
    assert(!find(forest, L"TresMapJump_dw025a_After_1"));
    assert(!find(forest, L""));
}
