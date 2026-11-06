#include "kh3ap/features/world/portals/map_jump_player_start_policy.h"
#include <cassert>
using namespace kh3ap;

int main()
{
    assert(map_jump_start_rank(L"tt_01_Lv_Save_01") > map_jump_start_rank(L"tt_01_AutoSave_01"));
    assert(map_jump_start_rank(L"dw_08_Lv_Start_01") > map_jump_start_rank(L"dw_08_Lv_Start_02"));
    assert(map_jump_start_rank(L"NormalSpawn") > map_jump_start_rank(L"GoA_Event_01"));
    assert(map_jump_start_rank(L"NormalPlayerStart") > map_jump_start_rank(L"Battle_Start_01"));
    assert(map_jump_start_rank(L"APKH02CastleStart", L"APKH02CastleStart") > map_jump_start_rank(L"dw_02_Lv_Save_01"));
    for (auto name : {L"he_02_enemy_aes", L"CameraStart", L"DebugSpawn", L"Cutscene_Start", L"test"})
    {
        assert(map_jump_start_rank(name) < 0);
    }
    assert(map_jump_start_rank(L"") >= 0); // Authored untagged PlayerStarts remain usable.
    assert(map_jump_start_rank(L"None") >= 0);
    MapJumpPlayerStartRequest request;
    const auto now = MapJumpPlayerStartRequest::Clock::time_point{};
    const auto map = L"/Game/Maps/APKH02Mirrors/APKH02Forest";
    const auto world = L"/Game/Maps/APKH02Mirrors/APKH02Forest.APKH02Forest";
    assert(!request.pending(now));
    request.arm(map, now);
    assert(request.pending(now));
    assert(request.matches(world, now));
    assert(!request.matches(L"/Game/Maps/APKH02Mirrors/APKH02ForestOther.APKH02ForestOther", now));
    assert(!request.matches(L"/Game/Maps/APKH02Mirrors/APKH02Forest.APKH02Forest:PersistentLevel.Actor", now));
    assert(!request.matches(L"/Game/Maps/APKH02Mirrors/APKH02Forest.WrongWorld", now));
    assert(!request.matches(world, now + std::chrono::seconds(90)));
    request.clear();
    assert(!request.matches(world, now)); // A subsequent death/retry cannot reuse it.
    request.arm(L"/Game/Levels/tt/tt_01/tt_01", now);
    assert(!request.matches(world, now));
    assert(request.matches(L"/Game/Levels/tt/tt_01/tt_01.tt_01", now));
}
