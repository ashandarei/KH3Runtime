#pragma once
#include "kh3ap/features/player/movement/arm_pose_blend.h"

namespace RC::Unreal
{
    class UObject;
}

namespace kh3ap::animation_pose
{
    // Non-owning views of the verified shipping extraction ABI. The engine
    // owns the backing arrays and stack allocator, including scratch poses.
    template <class T> struct ArrayView
    {
        T* data;
        int32_t count;
        int32_t capacity;
    };
    using Transform = arm_pose_blend::Transform;
    struct Pose
    {
        ArrayView<Transform> bones;
        const void* container;
    };
    struct Curve
    {
        ArrayView<uint64_t> elements;
        const void* uids;
        bool initialized;
    };
    struct Extraction
    {
        bool root_motion;
        float time;
        ArrayView<float> pose_curves;
    };
    static_assert(sizeof(Pose) == 24 && sizeof(Curve) == 32 && sizeof(Extraction) == 24);
    using Callback = void (*)(RC::Unreal::UObject*, Pose*, Curve*, const Extraction*, bool);
    enum class Consumer
    {
        CinematicProportions,
        KairiArms,
        Count
    };

    // Game-thread registration. One detour serves both consumers; never stack
    // two hooks at GetBonePose or re-enter callbacks for donor extraction.
    bool install(Consumer consumer, Callback callback);
    void extract(RC::Unreal::UObject* animation, Pose* pose, Curve* curve,
                 const Extraction* context, bool force_raw);
} // namespace kh3ap::animation_pose
