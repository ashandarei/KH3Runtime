#pragma once
#include <array>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <span>

namespace kh3ap::arm_pose_blend
{
    struct alignas(16) Transform
    {
        std::array<float, 4> rotation, translation, scale;
    };
    static_assert(sizeof(Transform) == 48);

    inline float recovery_weight(float phase, float start, float end = 1.0f)
    {
        const float progress = std::clamp((phase - start) / (end - start), 0.0f, 1.0f);
        // Ease both endpoints so recovery arrives at idle without residual
        // interpolation velocity when the native idle transition starts.
        return progress * progress * (3.0f - 2.0f * progress);
    }

    // Shortest-path normalized interpolation avoids a quaternion sign flip.
    inline std::array<float, 4> mix_rotation(const std::array<float, 4>& from,
                                           const std::array<float, 4>& to, float weight)
    {
        float dot = 0.0f;
        for (size_t axis = 0; axis < 4; ++axis) { dot += from[axis] * to[axis]; }
        const float sign = dot < 0.0f ? -1.0f : 1.0f;
        std::array<float, 4> result;
        float length = 0.0f;
        for (size_t axis = 0; axis < 4; ++axis)
        {
            result[axis] = from[axis] * (1.0f - weight) + to[axis] * weight * sign;
            length += result[axis] * result[axis];
        }
        const float inverse_length = 1.0f / std::sqrt(length);
        for (auto& value : result) { value *= inverse_length; }
        return result;
    }

    // Compact-pose order can differ at each mesh LOD. The mask is indexed by
    // mesh bone, never by the donor's skeleton or a cached compact-pose index.
    inline bool apply(std::span<Transform> target, std::span<const Transform> donor,
                      std::span<const uint16_t> mesh_indices, std::span<const bool> arm_mask)
    {
        if (target.size() != donor.size() || target.size() != mesh_indices.size()) { return false; }
        for (size_t index = 0; index < target.size(); ++index)
        {
            const auto mesh_index = mesh_indices[index];
            if (mesh_index >= arm_mask.size()) { return false; }
            if (!arm_mask[mesh_index]) { continue; }
            float norm = 0.0f;
            for (float value : donor[index].rotation)
            {
                if (!std::isfinite(value)) { return false; }
                norm += value * value;
            }
            if (std::abs(norm - 1.0f) > 0.01f) { return false; }
        }
        // Validate the entire operation before changing a single bone. Keep all
        // source translations/scales and every non-arm rotation byte-for-byte.
        for (size_t index = 0; index < target.size(); ++index)
        {
            if (arm_mask[mesh_indices[index]]) { target[index].rotation = donor[index].rotation; }
        }
        return true;
    }

    inline bool valid_rotation(const std::array<float, 4>& rotation)
    {
        float norm = 0.0f;
        for (const float value : rotation)
        {
            if (!std::isfinite(value)) { return false; }
            norm += value * value;
        }
        return std::abs(norm - 1.0f) <= 0.01f;
    }

    // Keep Kairi's impact legs; use one coherent donor torso/head/arm pose.
    // Recovery goes directly to her own idle, before the combat-stance tail.
    // The root transform, scale and translation padding are never changed.
    inline bool apply_landing(std::span<Transform> target, std::span<const Transform> donor,
                              std::span<const Transform> idle, std::span<const uint16_t> mesh_indices,
                              std::span<const bool> upper_body, float recovery)
    {
        if (target.size() != donor.size() || target.size() != idle.size() || target.size() != mesh_indices.size() ||
            !std::isfinite(recovery) || recovery < 0.0f || recovery > 1.0f) { return false; }
        for (size_t index = 0; index < target.size(); ++index)
        {
            const auto bone = mesh_indices[index];
            if (bone >= upper_body.size()) { return false; }
            if (bone == 0) { continue; }
            const auto& rotation = upper_body[bone] ? donor[index].rotation : target[index].rotation;
            if (!valid_rotation(rotation)) { return false; }
            if (recovery > 0.0f)
            {
                if (!valid_rotation(idle[index].rotation)) { return false; }
                for (size_t axis = 0; axis < 3; ++axis)
                {
                    if (!std::isfinite(target[index].translation[axis]) || !std::isfinite(idle[index].translation[axis]))
                    { return false; }
                }
            }
        }
        for (size_t index = 0; index < target.size(); ++index)
        {
            const auto bone = mesh_indices[index];
            if (bone == 0) { continue; }
            if (upper_body[bone]) { target[index].rotation = donor[index].rotation; }
            if (recovery > 0.0f)
            {
                target[index].rotation = mix_rotation(target[index].rotation, idle[index].rotation, recovery);
                for (size_t axis = 0; axis < 3; ++axis)
                {
                    target[index].translation[axis] += (idle[index].translation[axis] - target[index].translation[axis]) * recovery;
                }
            }
        }
        return true;
    }
}
