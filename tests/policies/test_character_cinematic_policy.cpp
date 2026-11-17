#include "kh3ap/features/player/characters/character_cinematic_policy.h"
#include "kh3ap/features/player/characters/cinematic_translation.h"
#include <array>
#include <cassert>
#include <limits>

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
    assert(proportion_animation_track(L"TresInterpTrackFullBodyAnimation"));
    assert(proportion_animation_track(L"TresInterpTrackSlotAnimation"));
    assert(proportion_animation_track(L"TresInterpTrackFaceAnimation"));
    assert(proportion_animation_track(L"TresInterpTrackEyeAnimation"));
    assert(proportion_animation_track(L"TresInterpTrackLipAnimation"));
    assert(!proportion_animation_track(L"TresInterpTrackWeaponAnimation"));
    assert(!proportion_animation_track(L"TresInterpTrackComponentAnimation"));
    assert(!proportion_animation_track(L"InterpTrackMove"));

    using namespace kh3ap::cinematic_translation;
    const Transform neutral{{0, 0, 0, 1}, {0, 0, 0, 0}, {1, 1, 1, 0}};
    std::array pose{neutral, neutral, neutral, neutral};
    // Compact LOD order differs from the mesh order. Bone zero is root;
    // bone two has no translated track and must retain its native value.
    const std::array<uint16_t, 4> indices{3, 0, 2, 1};
    const std::array offsets{Offset{}, Offset{{0, -9, 2}, true}, Offset{},
                             Offset{{0, -1, 3}, true}};
    pose[0].translation = {1, 4, 8, 0};       // Lip position in the source animation.
    pose[1].translation = {100, 200, 300, 0}; // Authored root motion.
    pose[2].translation = {5, 6, 7, 0};       // Receiver-only bone.
    pose[3].translation = {0, -100, 5, 0};    // Animated pelvis.
    const auto before = pose;
    assert(apply(pose, indices, offsets));
    assert((pose[0].translation == std::array<float, 4>{1, 3, 11, 0}));
    assert((pose[3].translation == std::array<float, 4>{0, -109, 7, 0}));
    assert(pose[1].translation == before[1].translation);
    assert(pose[2].translation == before[2].translation);
    for (size_t index = 0; index < pose.size(); ++index)
    {
        assert(pose[index].rotation == before[index].rotation);
        assert(pose[index].scale == before[index].scale);
    }
    auto moved = before;
    moved[0].translation[1] += 0.5f;
    assert(apply(moved, indices, offsets));
    assert(moved[0].translation[1] - pose[0].translation[1] == 0.5f); // Speech still moves.
    const auto valid = pose;
    assert(!apply(pose, std::array<uint16_t, 4>{3, 0, 2, 99}, offsets));
    assert(pose[0].translation == valid[0].translation); // Reject atomically.
    assert(!apply(pose, std::span(indices).first(3), offsets));
    auto invalid = offsets;
    invalid[1].translation[0] = std::numeric_limits<float>::infinity();
    assert(!apply(pose, indices, invalid));
    assert(pose[0].translation == valid[0].translation);

    struct Bone
    {
        int name;
        int parent;
    };
    const std::array source{Bone{0, -1}, Bone{3, 0}, Bone{1, 0}, Bone{7, 0}};
    const std::array target{Bone{0, -1}, Bone{1, 0}, Bone{2, 0}, Bone{3, 0}};
    std::array source_pose{neutral, neutral, neutral, neutral};
    std::array target_pose{neutral, neutral, neutral, neutral};
    source_pose[1].translation = {1, 2, 3, 0};
    target_pose[3].translation = {1, 1, 6, 0};
    source_pose[2].translation = {0, -100, 0, 0};
    target_pose[1].translation = {0, -109, 2, 0};
    // Root, reordered lip/pelvis, unmatched source and invalid(-1) tracks.
    const std::array<int32_t, 5> tracks{0, 1, 2, 3, -1};
    const auto matched = match_tracks<Bone>(tracks, source, source_pose, target, target_pose);
    assert(!matched[0].animated && !matched[2].animated);
    assert(matched[1].animated && matched[3].animated);
    assert(matched[1].translation == offsets[1].translation);
    assert(matched[3].translation == offsets[3].translation);
    const auto partial =
        match_tracks<Bone>(std::span(tracks).first(2), source, source_pose, target, target_pose);
    assert(!partial[1].animated && partial[3].animated);
    bool rejected = false;
    try
    {
        match_tracks<Bone>(std::array<int32_t, 1>{99}, source, source_pose, target, target_pose);
    }
    catch (const std::runtime_error&)
    {
        rejected = true;
    }
    assert(rejected);
}
