#include "kh3ap/features/player/characters/character_cinematic_policy.h"
#include <array>
#include <cassert>

using namespace kh3ap::character_cinematic_binding;

int main()
{
    constexpr auto scene =
        L"/Game/Levels/he/he_03/umap/he_03_camera_ms027.he_03_camera_ms027:PersistentLevel.ms027";
    std::array roles{PlayerRole{true, false, 0}, PlayerRole{true, false, 0}};
    assert(can_share_player_roles(scene, roles));
    roles[0] = {true, true, 42};
    assert(can_share_player_roles(scene, roles));
    roles[1] = {true, true, 42};
    assert(can_share_player_roles(scene, roles));
    roles[1].actor = 43;
    assert(!can_share_player_roles(scene, roles)); // Distinct story actors.
    roles[1] = {true, false, 42};
    assert(!can_share_player_roles(scene, roles)); // Scenery/stale slot.
    roles[1] = {false, true, 42};
    assert(!can_share_player_roles(scene, roles)); // Animation or movement.
    roles[1] = {true, true, 42};
    assert(!can_share_player_roles(L"/Game/Cutscene/he108.he108:PersistentLevel.Movie", roles));
    assert(!can_share_player_roles(scene, {}));
    assert(!can_share_player_roles(scene, std::span(roles).first(1)));
    assert(presentation_track(L"TresInterpTrackVisibility"));
    assert(presentation_track(L"TresInterpTrackWeaponControl"));
    assert(!presentation_track(L"InterpTrackAnimControl"));
    assert(!presentation_track(L"InterpTrackMove"));
    assert(!presentation_track(L"InterpTrackEvent"));
    assert(!presentation_track(L"UnknownTrack"));
}
