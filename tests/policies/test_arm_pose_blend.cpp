#include "kh3ap/features/player/movement/arm_pose_blend.h"
#include <cassert>
#include <cstring>
#include <limits>

int main()
{
    using namespace kh3ap::arm_pose_blend;
    assert(recovery_weight(0.0f, 0.35f) == 0.0f);
    assert(recovery_weight(0.35f, 0.35f) == 0.0f);
    assert(std::abs(recovery_weight(0.675f, 0.35f) - 0.5f) < 0.00001f);
    assert(recovery_weight(1.0f, 0.35f) == 1.0f);
    assert(recovery_weight(1.1f, 0.35f) == 1.0f);
    assert(recovery_weight(0.8f, 0.85f) == 0.0f);
    assert(recovery_weight(0.36f, 0.35f) < 0.001f);
    assert(recovery_weight(0.99f, 0.35f) > 0.999f);
    const std::array<float, 4> neutral{0, 0, 0, 1};
    const std::array<float, 4> turned{1, 0, 0, 0};
    assert(mix_rotation(neutral, turned, 0.0f) == neutral);
    assert(mix_rotation(neutral, turned, 1.0f) == turned);
    const auto halfway = mix_rotation(neutral, turned, 0.5f);
    assert(std::abs(halfway[0] - std::sqrt(0.5f)) < 0.00001f);
    assert(std::abs(halfway[3] - std::sqrt(0.5f)) < 0.00001f);
    const std::array<float, 4> opposite_sign{0, 0, 0, -1};
    assert(mix_rotation(neutral, opposite_sign, 0.5f) == neutral);
    std::array<Transform, 4> original{};
    for (int i = 0; i < 4; ++i)
    {
        original[i] = {{0, 0, 0, 1}, {float(i), 2, 3, 99}, {1, 1, 1, 77}};
    }
    auto donor = original;
    for (auto& bone : donor)
    {
        bone.rotation = {1, 0, 0, 0};
        bone.translation = {900, 800, 700, 600};
        bone.scale = {4, 5, 6, 7};
    }
    const std::array<uint16_t, 4> compact{0, 7, 2, 9};
    std::array<bool, 10> mask{};
    mask[7] = mask[9] = true;
    auto target = original;
    assert(apply(target, donor, compact, mask));
    assert(std::memcmp(&target[0], &original[0], sizeof(Transform)) == 0);
    assert(std::memcmp(&target[2], &original[2], sizeof(Transform)) == 0);
    for (int i : {1, 3})
    {
        assert(target[i].rotation == donor[i].rotation);
        assert(target[i].translation == original[i].translation);
        assert(target[i].scale == original[i].scale);
    }
    // Reject stale LOD mappings atomically, including errors at the last bone.
    auto invalid = compact;
    invalid[3] = 10;
    target = original;
    assert(!apply(target, donor, invalid, mask));
    assert(std::memcmp(target.data(), original.data(), sizeof(target)) == 0);

    // Landing: indices 7/9 represent torso/head; index 2 represents a leg.
    // The impact leg remains exact, and Sora's translations/scales never leak.
    donor = original;
    for (auto& bone : donor)
    {
        bone.rotation = turned;
        bone.translation = {900, 800, 700, 600};
        bone.scale = {4, 5, 6, 7};
    }
    auto idle = original;
    for (auto& bone : idle)
    {
        bone.rotation = {0, 1, 0, 0};
        bone.translation = {5, 6, 7, 8};
    }
    mask[7] = mask[9] = true;
    target = original;
    assert(apply_landing(target, donor, idle, compact, mask, 0.0f));
    assert(std::memcmp(&target[0], &original[0], sizeof(Transform)) == 0);
    assert(std::memcmp(&target[2], &original[2], sizeof(Transform)) == 0);
    assert(target[1].rotation == donor[1].rotation);
    assert(target[1].translation == original[1].translation);
    // Run-stop recovery keeps the braking pose initially, then eases even
    // unmasked legs toward standing while preserving root motion and scale.
    assert(recovery_weight(0.15f, 0.15f, 0.8f) == 0.0f);
    const float stop_halfway = recovery_weight(0.475f, 0.15f, 0.8f);
    assert(std::abs(stop_halfway - 0.5f) < 0.00001f);
    target = original;
    assert(apply_landing(target, donor, idle, compact, mask, stop_halfway));
    assert(std::memcmp(&target[0], &original[0], sizeof(Transform)) == 0);
    assert(target[2].rotation == mix_rotation(original[2].rotation, idle[2].rotation, stop_halfway));
    assert(target[1].rotation == mix_rotation(donor[1].rotation, idle[1].rotation, stop_halfway));
    for (int index : {1, 2, 3})
    {
        for (int axis = 0; axis < 3; ++axis)
        {
            assert(std::abs(target[index].translation[axis] -
                (original[index].translation[axis] + (idle[index].translation[axis] - original[index].translation[axis]) * stop_halfway)) < 0.00001f);
        }
        assert(target[index].scale == original[index].scale);
    }
    target = original;
    assert(apply_landing(target, donor, idle, compact, mask, 1.0f));
    assert(std::memcmp(&target[0], &original[0], sizeof(Transform)) == 0);
    for (int index : {1, 2, 3})
    {
        assert(target[index].rotation == idle[index].rotation);
        for (int axis = 0; axis < 3; ++axis) { assert(target[index].translation[axis] == idle[index].translation[axis]); }
        assert(target[index].translation[3] == original[index].translation[3]);
        assert(target[index].scale == original[index].scale);
    }
    assert(recovery_weight(0.25f, 0.25f, 0.8f) == 0.0f);
    assert(recovery_weight(0.8f, 0.25f, 0.8f) == 1.0f);
    target = original;
    idle[3].rotation[0] = std::numeric_limits<float>::quiet_NaN();
    assert(!apply_landing(target, donor, idle, compact, mask, 0.5f));
    assert(std::memcmp(target.data(), original.data(), sizeof(target)) == 0);
    assert(!apply_landing(target, donor, idle, invalid, mask, 0.0f));
    assert(std::memcmp(target.data(), original.data(), sizeof(target)) == 0);
    donor[3].rotation[0] = std::numeric_limits<float>::quiet_NaN();
    assert(!apply(target, donor, compact, mask));
    assert(std::memcmp(target.data(), original.data(), sizeof(target)) == 0);
    donor[3].rotation = {2, 0, 0, 0};
    assert(!apply(target, donor, compact, mask));
    assert(!apply(target, std::span(donor).first(3), compact, mask));
    mask.fill(false);
    assert(apply(target, donor, compact, mask));
    assert(std::memcmp(target.data(), original.data(), sizeof(target)) == 0);
}
