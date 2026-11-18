#pragma once
#include "kh3ap/features/player/movement/arm_pose_blend.h"
#include <vector>
#include <stdexcept>

namespace kh3ap::cinematic_translation
{
    using Transform = arm_pose_blend::Transform;
    struct Offset
    {
        std::array<float, 3> translation{};
        bool animated{};
    };

    template <class Bone>
    auto match_tracks(std::span<const int32_t> tracks, std::span<const Bone> source,
                      std::span<const Transform> source_pose, std::span<const Bone> target,
                      std::span<const Transform> target_pose) -> std::vector<Offset>
    {
        if (source.size() != source_pose.size() || target.size() != target_pose.size())
        {
            throw std::runtime_error("cinematic_reference_count");
        }
        std::vector<Offset> result(target.size());
        for (const auto source_index : tracks)
        {
            if (source_index == -1)
            {
                continue;
            }
            if (source_index < 0 || static_cast<size_t>(source_index) >= source.size())
            {
                throw std::runtime_error("cinematic_track_map_index");
            }
            for (size_t target_index = 0; target_index < target.size(); ++target_index)
            {
                // Preserve authored root motion and do not assume matching
                // names occupy matching indices in different character rigs.
                if (target[target_index].parent == -1 ||
                    target[target_index].name != source[source_index].name)
                {
                    continue;
                }
                auto& offset = result[target_index];
                offset.animated = true;
                for (size_t axis = 0; axis < 3; ++axis)
                {
                    offset.translation[axis] = target_pose[target_index].translation[axis] -
                                               source_pose[source_index].translation[axis];
                }
                break;
            }
        }
        return result;
    }

    // Offsets are indexed by receiving mesh bone, not source skeleton or
    // compact-pose index. Only translated tracks get a reference-pose offset;
    // missing tracks already contain the receiving mesh's reference position.
    inline bool apply(std::span<Transform> pose, std::span<const uint16_t> indices,
                      std::span<const Offset> offsets)
    {
        if (pose.size() != indices.size())
        {
            return false;
        }
        for (size_t index = 0; index < pose.size(); ++index)
        {
            if (indices[index] >= offsets.size())
            {
                return false;
            }
            const auto& offset = offsets[indices[index]];
            if (!offset.animated)
            {
                continue;
            }
            for (size_t axis = 0; axis < 3; ++axis)
            {
                if (!std::isfinite(pose[index].translation[axis] + offset.translation[axis]))
                {
                    return false;
                }
            }
        }
        for (size_t index = 0; index < pose.size(); ++index)
        {
            const auto& offset = offsets[indices[index]];
            if (!offset.animated)
            {
                continue;
            }
            for (size_t axis = 0; axis < 3; ++axis)
            {
                pose[index].translation[axis] += offset.translation[axis];
            }
        }
        return true;
    }
} // namespace kh3ap::cinematic_translation
