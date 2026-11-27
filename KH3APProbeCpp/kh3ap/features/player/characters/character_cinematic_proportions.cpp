#include "character_cinematic_proportions.h"
#include "cinematic_translation.h"
#include "kh3ap/features/player/animation_pose.h"
#include "kh3ap/core/objects/actor_owning_world.h"
#include "kh3ap/core/objects/uobject_weak.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/core/performance_build.h"
#include <Unreal/UObject.hpp>
#include <Unreal/UClass.hpp>
#include <Unreal/Property/FArrayProperty.hpp>
#include <Unreal/Property/FStructProperty.hpp>
#include <Unreal/UScriptStruct.hpp>
#include <Unreal/TArray.hpp>
#include <DynamicOutput/DynamicOutput.hpp>
#include <atomic>
#include <algorithm>
#include <stdexcept>

namespace kh3ap::character_cinematic_proportions
{
    using namespace RC::Unreal;
    using animation_pose::ArrayView;
    using animation_pose::Transform;
    namespace
    {
        constexpr int maximum_bones = 1024;
        struct BoneInfo
        {
            FName name;
            int32_t parent;
            int32_t padding;
        };
        struct ReferenceSkeleton
        {
            ArrayView<BoneInfo> raw_bones;
            ArrayView<Transform> raw_pose;
            ArrayView<BoneInfo> bones;
            ArrayView<Transform> pose;
        };
        static_assert(sizeof(BoneInfo) == 16 && sizeof(ReferenceSkeleton) == 64);

        template <class T> bool valid_array(const ArrayView<T>& array)
        {
            return array.count >= 0 && array.count <= maximum_bones &&
                   array.capacity >= array.count && array.capacity <= 4096 &&
                   (!array.count || array.data);
        }

        ReferenceSkeleton* reference(UObject* asset, bool mesh)
        {
            auto* anchor =
                is_uobject_valid_fast(asset)
                    ? asset->GetPropertyByNameInChain(mesh ? L"MorphTargets" : L"RefLocalPoses")
                    : nullptr;
            if (!anchor || !anchor->IsA<FArrayProperty>() || anchor->GetElementSize() != 16)
            {
                throw std::runtime_error("cinematic_reference_anchor");
            }
            // Verified shipping layout: a mesh's resource fence follows
            // MorphTargets; a skeleton's reference immediately follows the
            // deprecated RefLocalPoses array. No engine-owned object is copied.
            auto* result = reinterpret_cast<ReferenceSkeleton*>(
                anchor->ContainerPtrToValuePtr<std::byte>(asset) + 16 + (mesh ? 8 : 0));
            if (!valid_array(result->raw_bones) || !valid_array(result->raw_pose) ||
                !valid_array(result->bones) || !valid_array(result->pose) ||
                result->raw_bones.count != result->raw_pose.count ||
                result->bones.count != result->pose.count || result->bones.count < 2 ||
                result->bones.data[0].parent != -1)
            {
                throw std::runtime_error("cinematic_reference_layout");
            }
            for (int index = 0; index < result->bones.count; ++index)
            {
                if (result->bones.data[index].parent >= index ||
                    result->bones.data[index].parent < -1 ||
                    !arm_pose_blend::valid_rotation(result->pose.data[index].rotation))
                {
                    throw std::runtime_error("cinematic_reference_bone");
                }
                for (int axis = 0; axis < 3; ++axis)
                {
                    if (!std::isfinite(result->pose.data[index].translation[axis]))
                    {
                        throw std::runtime_error("cinematic_reference_translation");
                    }
                }
            }
            return result;
        }

        struct Sequence
        {
            FWeakObjectPtr animation;
            std::vector<cinematic_translation::Offset> raw, compressed;
        };

        void validate_animation_translations(UObject* skeleton, int count)
        {
            auto* field = skeleton->GetPropertyByNameInChain(L"BoneTree");
            auto* inner = field && field->IsA<FArrayProperty>()
                              ? static_cast<FArrayProperty*>(field)->GetInner()
                              : nullptr;
            auto* schema = inner && inner->IsA<FStructProperty>()
                               ? static_cast<FStructProperty*>(inner)->GetStruct()
                               : nullptr;
            auto* mode =
                schema ? find_struct_field_by_name(schema, L"TranslationRetargetingMode") : nullptr;
            if (!mode || inner->GetElementSize() != 16 || mode->GetElementSize() != 1 ||
                mode->GetOffset_Internal() != 12)
            {
                throw std::runtime_error("cinematic_translation_mode_layout");
            }
            const auto* tree = field->ContainerPtrToValuePtr<FScriptArray>(skeleton);
            if (tree->Num() != count)
            {
                throw std::runtime_error("cinematic_translation_mode_count");
            }
            for (int index = 0; index < count; ++index)
            {
                const auto* value = static_cast<const std::byte*>(tree->GetData()) + index * 16;
                if (*mode->ContainerPtrToValuePtr<uint8_t>(value) != 0)
                {
                    throw std::runtime_error("cinematic_translation_already_retargeted");
                }
            }
        }
    } // namespace

    struct Plan
    {
        FWeakObjectPtr pawn, world, mesh_asset;
        size_t reference_offset{};
        std::vector<Sequence> sequences;
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
        std::atomic<uint64_t> applied{}, rejected{};
#endif
    };

    namespace
    {
        std::atomic<std::shared_ptr<const std::vector<Lease>>> current;
        // Game-thread publishers share one immutable snapshot with animation workers.
        std::vector<Lease> cinematic_plans;
        Lease reaction_plan;

        void evaluate(UObject* animation, animation_pose::Pose* pose, animation_pose::Curve*,
                      const animation_pose::Extraction*, bool force_raw)
        {
            const auto plans = current.load(std::memory_order_acquire);
            if (!plans || !pose || !pose->container || !valid_array(pose->bones))
            {
                return;
            }
            for (const auto& plan : *plans)
            {
                auto* asset = plan->mesh_asset.Get();
                if (!asset || !plan->pawn.Get() || !plan->world.Get())
                {
                    continue;
                }
                // Audited FBoneContainer header: required-bone array, inline
                // bit array, three weak assets, then its reference skeleton.
                // Match the actual receiving mesh before touching its pose.
                const auto* ref = *reinterpret_cast<const void* const*>(
                    static_cast<const std::byte*>(pose->container) + 0x48);
                if (ref != reinterpret_cast<std::byte*>(asset) + plan->reference_offset)
                {
                    continue;
                }
                for (const auto& sequence : plan->sequences)
                {
                    if (sequence.animation.Get() != animation)
                    {
                        continue;
                    }
                    const auto* indices = static_cast<const ArrayView<uint16_t>*>(pose->container);
                    const bool applied =
                        valid_array(*indices) &&
                        cinematic_translation::apply(
                            {pose->bones.data, static_cast<size_t>(pose->bones.count)},
                            {indices->data, static_cast<size_t>(indices->count)},
                            force_raw ? sequence.raw : sequence.compressed);
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
                    if (applied)
                    {
                        ++plan->applied;
                    }
                    else
                    {
                        ++plan->rejected;
                    }
#endif
                    return; // Overlapping groups must never add an offset twice.
                }
            }
        }

        auto offsets(UObject* animation, ReferenceSkeleton* source, ReferenceSkeleton* target,
                     bool compressed) -> std::vector<cinematic_translation::Offset>
        {
            auto* field = animation->GetPropertyByNameInChain(L"TrackToSkeletonMapTable");
            auto* inner = field && field->IsA<FArrayProperty>()
                              ? static_cast<FArrayProperty*>(field)->GetInner()
                              : nullptr;
            auto* schema = inner && inner->IsA<FStructProperty>()
                               ? static_cast<FStructProperty*>(inner)->GetStruct()
                               : nullptr;
            auto* index_field =
                schema ? find_struct_field_by_name(schema, L"BoneTreeIndex") : nullptr;
            if (!index_field || index_field->GetOffset_Internal() != 0 ||
                inner->GetElementSize() != 4 || index_field->GetElementSize() != 4)
            {
                throw std::runtime_error("cinematic_track_map_layout");
            }
            const auto* tracks = reinterpret_cast<const ArrayView<int32_t>*>(
                field->ContainerPtrToValuePtr<std::byte>(animation) + (compressed ? 16 : 0));
            if (!valid_array(*tracks))
            {
                throw std::runtime_error("cinematic_track_map_count");
            }
            return cinematic_translation::match_tracks<BoneInfo>(
                {tracks->data, static_cast<size_t>(tracks->count)},
                {source->bones.data, static_cast<size_t>(source->bones.count)},
                {source->pose.data, static_cast<size_t>(source->pose.count)},
                {target->bones.data, static_cast<size_t>(target->bones.count)},
                {target->pose.data, static_cast<size_t>(target->pose.count)});
        }
    } // namespace

    Lease prepare(UObject* pawn, const std::vector<UObject*>& animations)
    {
        if (animations.empty())
        {
            return {};
        }
        auto result = std::make_shared<Plan>();
        auto* mesh = try_read_reflected_object_property_value(pawn, L"MyMesh");
        auto* asset = is_uobject_valid_fast(mesh)
                          ? try_read_reflected_object_property_value(mesh, L"SkeletalMesh")
                          : nullptr;
        auto* target = reference(asset, true);
        result->pawn = make_uobject_weak(pawn);
        result->world = make_uobject_weak(actor_owning_world(pawn));
        result->mesh_asset = make_uobject_weak(asset);
        result->reference_offset =
            reinterpret_cast<std::byte*>(target) - reinterpret_cast<std::byte*>(asset);
        for (auto* animation : animations)
        {
            if (!is_uobject_valid_fast(animation) ||
                animation->GetClassPrivate()->GetName() != L"AnimSequence")
            {
                throw std::runtime_error("cinematic_sequence_type");
            }
            auto* additive = animation->GetPropertyByNameInChain(L"AdditiveAnimType");
            auto* source_name = animation->GetPropertyByNameInChain(L"RetargetSource");
            if (!additive || additive->GetElementSize() != 1 || !source_name ||
                source_name->GetElementSize() != sizeof(FName))
            {
                throw std::runtime_error("cinematic_sequence_layout");
            }
            // Additive tracks already contain deltas, not reference positions.
            if (*additive->ContainerPtrToValuePtr<uint8_t>(animation) != 0)
            {
                continue;
            }
            if (source_name->ContainerPtrToValuePtr<FName>(animation)->ToString() != L"None")
            {
                throw std::runtime_error("cinematic_named_reference_pose");
            }
            auto* skeleton = try_read_reflected_object_property_value(animation, L"Skeleton");
            auto* source = reference(skeleton, false);
            // The offset follows raw animated translations. Do not compound
            // another mod's/native retarget mode or mutate a shared skeleton.
            validate_animation_translations(skeleton, source->raw_bones.count);
            result->sequences.push_back({make_uobject_weak(animation),
                                         offsets(animation, source, target, false),
                                         offsets(animation, source, target, true)});
        }
        if (result->sequences.empty())
        {
            return {};
        }
        if (!animation_pose::install(animation_pose::Consumer::CinematicProportions, &evaluate))
        {
            throw std::runtime_error("cinematic_pose_hook");
        }
        return result;
    }

    static void publish_current()
    {
        auto plans = cinematic_plans;
        if (reaction_plan) { plans.push_back(reaction_plan); }
        auto next =
            plans.empty() ? nullptr : std::make_shared<const std::vector<Lease>>(std::move(plans));
        const auto previous = current.exchange(std::move(next), std::memory_order_acq_rel);
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
        if (previous)
        {
            const auto active = current.load();
            for (const auto& plan : *previous)
            {
                if (!active || std::find(active->begin(), active->end(), plan) == active->end())
                {
                    RC::Output::send<RC::LogLevel::Normal>(
                        STR("[KH3APProbeCpp] CharacterCinematic proportions released applied={} "
                            "rejected={}\n"),
                        plan->applied.load(), plan->rejected.load());
                }
            }
        }
#endif
    }

    void publish(std::vector<Lease> plans)
    {
        cinematic_plans = std::move(plans);
        publish_current();
    }

    void publish_reaction(Lease plan)
    {
        if (reaction_plan != plan)
        {
            reaction_plan = std::move(plan);
            publish_current();
        }
    }
} // namespace kh3ap::character_cinematic_proportions
