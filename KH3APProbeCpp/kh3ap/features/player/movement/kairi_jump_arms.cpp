#include "kh3ap/features/player/characters/character_profile.h"
#include "kh3ap/hooks/native_detour_lifetime.h"
#include <polyhook2/Detour/x64Detour.hpp>
#include "kairi_jump_arms.h"
#include "kh3ap/features/player/animation_pose.h"
#include "arm_pose_blend.h"
#include "kh3ap/features/player/player_animation_support.h"
#include "kh3ap/features/kh02/core/kh02_reflected_call.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/core/objects/uobject_weak.h"
#include <Unreal/UObject.hpp>
#include <Unreal/UClass.hpp>
#include <Unreal/UObjectGlobals.hpp>
#include <Unreal/Property/FArrayProperty.hpp>
#include <Unreal/Property/FStructProperty.hpp>
#include <Unreal/UScriptStruct.hpp>
#include <Unreal/TArray.hpp>
#include <array>
#include <atomic>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <vector>
#define NOMINMAX
#include <Windows.h>

namespace kh3ap::kairi_jump_arms
{
    using namespace RC::Unreal;
    namespace
    {
        constexpr size_t max_bones = 1024;
        constexpr size_t max_curves = 2048;
        constexpr std::array names{L"JumpNormalDownLoop", L"JumpHighStart", L"JumpHighRiseLoop",
                                   L"JumpDoubleFlightStart", L"JumpDoubleFlightRiseLoop",
                                   L"Land", L"LightLand", L"RollLand"};
        using animation_pose::ArrayView;
        using animation_pose::Transform;
        using animation_pose::Pose;
        using animation_pose::Curve;
        using animation_pose::Extraction;

        struct Motion
        {
            FWeakObjectPtr source, donor;
            FName normal;
            bool takeoff{}, landing{}, upper_body{}, running{}, stopping{};
            float relaxed_start{0.5f};
            float source_length{}, donor_length{};
        };
        struct Binding
        {
            FWeakObjectPtr pawn, anim, mesh;
            FWeakObjectPtr landing_idle;
            std::mutex idle_mutex;
            std::array<Transform, max_bones> idle_pose{};
            std::array<bool, max_bones> idle_present{};
            std::atomic<bool> idle_refresh{true};
            size_t slot_offset{};
            std::vector<Motion> motions;
            std::array<bool, max_bones> arms{};
            std::array<bool, max_bones> upper_body{};
            std::array<bool, max_bones> run_upper_body{};
            int bone_count{}, arm_count{}, upper_body_count{}, run_upper_body_count{};
            std::atomic<bool> unarmed{}, jumping{};
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
            std::atomic<uint64_t> idle_captures{};
            std::atomic<float> idle_capture_time{};
            std::atomic<uint64_t> blended{}, rejected{}, armed_samples{}, evaluations{};
            std::atomic<uint64_t> run_blended{}, run_armed_samples{};
            std::atomic<uint64_t> run_stop_blended{}, run_stop_recovered{};
            std::array<std::atomic<uint64_t>, names.size()> samples{};
#endif
        };
        std::atomic<std::shared_ptr<Binding>> current;
        std::atomic<void*> current_slot{};
        thread_local Binding* evaluating{};
        thread_local float evaluating_idle_time{-1.0f};
        uint64_t original_slot{};
        std::unique_ptr<PLH::x64Detour> slot_hook;
        using EvaluateSlot = void (*)(void*, void*);

        UObject* object(UObject* owner, const wchar_t* field)
        {
            return is_uobject_valid_fast(owner) ? try_read_reflected_object_property_value(owner, field) : nullptr;
        }

        void capture_idle(Binding* binding, Pose* pose, float time)
        {
            if (!binding->idle_refresh.load(std::memory_order_relaxed) || time < 0.25f || pose->bones.count <= 0 || pose->bones.count > max_bones ||
                !pose->bones.data || !pose->container) { return; }
            const auto* indices = static_cast<const ArrayView<uint16_t>*>(pose->container);
            if (indices->data && indices->count == pose->bones.count)
            {
                std::lock_guard lock(binding->idle_mutex);
                bool missing = false;
                for (int index = 0; index < indices->count; ++index)
                {
                    const auto bone = indices->data[index];
                    if (bone >= binding->bone_count) { return; }
                    missing |= !binding->idle_present[bone];
                    if (!arm_pose_blend::valid_rotation(pose->bones.data[index].rotation)) { return; }
                }
                // Capture actual native playback once per required-bone set.
                // Never replace it with a separately extracted frame zero.
                if (missing)
                {
                    for (int index = 0; index < indices->count; ++index)
                    {
                        const auto bone = indices->data[index];
                        binding->idle_pose[bone] = pose->bones.data[index];
                        binding->idle_present[bone] = true;
                    }
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
                    ++binding->idle_captures;
                    binding->idle_capture_time.store(time);
#endif
                }
                binding->idle_refresh.store(false, std::memory_order_relaxed);
            }
        }

        void evaluate_pose(UObject* animation, Pose* pose, Curve* curve, const Extraction* context, bool force_raw)
        {
            auto original = &animation_pose::extract;
            auto* binding = evaluating;
            if (!binding) { return; }
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
            ++binding->evaluations;
#endif
            if (animation == binding->landing_idle.Get())
            {
                evaluating_idle_time = context->time;
                return;
            }
            for (size_t motion_index = 0; motion_index < binding->motions.size(); ++motion_index)
            {
                const auto& motion = binding->motions[motion_index];
                if (motion.source.Get() != animation) { continue; }
                if (!motion.running) { binding->jumping.store(true, std::memory_order_relaxed); }
                if (!binding->unarmed.load(std::memory_order_relaxed))
                {
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
                    if (motion.running) { ++binding->run_armed_samples; }
                    else { ++binding->armed_samples; }
#endif
                    return;
                }
                auto* donor = motion.donor.Get();
                const auto* indices = static_cast<const ArrayView<uint16_t>*>(pose->container);
                if (!donor || !indices || !indices->data || !pose->bones.data ||
                    pose->bones.count <= 0 || pose->bones.count > max_bones ||
                    indices->count != pose->bones.count || indices->capacity < indices->count ||
                    curve->elements.count < 0 || curve->elements.count > max_curves ||
                    (curve->elements.count && !curve->elements.data) || !std::isfinite(context->time))
                {
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
                    ++binding->rejected;
#endif
                    return;
                }
                for (int i = 0; i < indices->count; ++i)
                {
                    if (indices->data[i] >= binding->bone_count)
                    {
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
                        ++binding->rejected;
#endif
                        return;
                    }
                }
                // Evaluate the donor into separate scratch storage. Its curves,
                // root motion, translation, scale and notifications never reach
                // Kairi. Jump/roll replace arms; landing and run stops recover
                // from the donor upper body into Kairi's own standing pose.
                std::array<Transform, max_bones> donor_bones;
                std::array<Transform, max_bones> relaxed_bones;
                std::array<uint64_t, max_curves> donor_curves;
                Pose donor_pose{{donor_bones.data(), pose->bones.count, static_cast<int32_t>(max_bones)}, pose->container};
                Curve donor_curve{{donor_curves.data(), curve->elements.count, static_cast<int32_t>(max_curves)}, curve->uids, curve->initialized};
                std::memcpy(donor_bones.data(), pose->bones.data, pose->bones.count * sizeof(Transform));
                if (curve->elements.count)
                {
                    std::memcpy(donor_curves.data(), curve->elements.data, curve->elements.count * sizeof(uint64_t));
                }
                auto donor_context = *context;
                donor_context.root_motion = false;
                donor_context.time = motion.takeoff ? 0.0f :
                    std::clamp(context->time / motion.source_length, 0.0f, 1.0f) * motion.donor_length;
                original(donor, &donor_pose, &donor_curve, &donor_context, false);
                if (donor_pose.bones.count != pose->bones.count || donor_pose.container != pose->container)
                {
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
                    ++binding->rejected;
#endif
                    return;
                }
                const float phase = std::clamp(context->time / motion.source_length, 0.0f, 1.0f);
                // The authored start/end may still contain a weapon grip.
                // Ease from relaxed arms on takeoff, and back to them over the
                // landing recovery, without changing the body's timing.
                const float relaxed_weight = motion.stopping ? arm_pose_blend::recovery_weight(phase, 0.15f, 0.8f) :
                    motion.takeoff ? 1.0f - arm_pose_blend::recovery_weight(phase, 0.0f) :
                    (motion.landing ? arm_pose_blend::recovery_weight(phase, motion.relaxed_start, motion.upper_body ? 0.8f : 1.0f) : 0.0f);
                if (relaxed_weight > 0.0f)
                {
                    {
                        std::lock_guard lock(binding->idle_mutex);
                        for (int index = 0; index < pose->bones.count; ++index)
                        {
                            const auto bone = indices->data[index];
                            if (!binding->idle_present[bone])
                            {
                                binding->idle_refresh.store(true, std::memory_order_relaxed);
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
                                ++binding->rejected;
#endif
                                return;
                            }
                            relaxed_bones[index] = binding->idle_pose[bone];
                        }
                    }
                    for (int index = 0; index < pose->bones.count; ++index)
                    {
                        if (!motion.upper_body && !motion.stopping && binding->arms[indices->data[index]])
                        {
                            donor_pose.bones.data[index].rotation = arm_pose_blend::mix_rotation(
                                donor_pose.bones.data[index].rotation, relaxed_bones[index].rotation, relaxed_weight);
                        }
                    }
                }
                const bool applied = (motion.upper_body || motion.stopping) ? arm_pose_blend::apply_landing(
                        {pose->bones.data, static_cast<size_t>(pose->bones.count)},
                        {donor_pose.bones.data, static_cast<size_t>(donor_pose.bones.count)},
                        {relaxed_bones.data(), static_cast<size_t>(pose->bones.count)},
                        {indices->data, static_cast<size_t>(indices->count)},
                        {motion.stopping ? binding->run_upper_body.data() : binding->upper_body.data(),
                         static_cast<size_t>(binding->bone_count)}, relaxed_weight) :
                    arm_pose_blend::apply(
                        {pose->bones.data, static_cast<size_t>(pose->bones.count)},
                        {donor_pose.bones.data, static_cast<size_t>(donor_pose.bones.count)},
                        {indices->data, static_cast<size_t>(indices->count)},
                        {motion.running ? binding->run_upper_body.data() : binding->arms.data(),
                         static_cast<size_t>(binding->bone_count)});
                if (!applied)
                {
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
                    ++binding->rejected;
#endif
                    return;
                }
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
                if (motion.running)
                {
                    ++binding->run_blended;
                    if (motion.stopping)
                    {
                        ++binding->run_stop_blended;
                        if (relaxed_weight >= 0.999f) { ++binding->run_stop_recovered; }
                    }
                }
                else
                {
                    ++binding->samples[motion_index];
                    ++binding->blended;
                }
#endif
                return;
            }
        }

        void evaluate_slot(void* slot, void* output)
        {
            if (slot != current_slot.load(std::memory_order_relaxed))
            {
                reinterpret_cast<EvaluateSlot>(original_slot)(slot, output);
                return;
            }
            const auto binding = current.load(std::memory_order_acquire);
            auto* previous = evaluating;
            const float previous_idle_time = evaluating_idle_time;
            evaluating_idle_time = -1.0f;
            evaluating = nullptr;
            if (binding)
            {
                auto* anim = binding->anim.Get();
                if (anim && slot == reinterpret_cast<std::byte*>(anim) + binding->slot_offset &&
                    binding->pawn.Get() && binding->mesh.Get())
                {
                    evaluating = binding.get();
                    binding->jumping.store(false, std::memory_order_relaxed);
                }
            }
            reinterpret_cast<EvaluateSlot>(original_slot)(slot, output);
            if (evaluating && !binding->jumping.load(std::memory_order_relaxed))
            {
                // Audited FPoseContext: animation proxy pointer followed by
                // FCompactPose at +8. Capture the final FullBody output after
                // native sequence processing and blending, not raw extraction.
                capture_idle(binding.get(), reinterpret_cast<Pose*>(static_cast<std::byte*>(output) + 8), evaluating_idle_time);
            }
            evaluating = previous;
            evaluating_idle_time = previous_idle_time;
        }

        bool install()
        {
            if (!animation_pose::install(animation_pose::Consumer::KairiArms, &evaluate_pose)) { return false; }
            if (slot_hook && slot_hook->isHooked() && original_slot) { return true; }
            if (!release_native_detour(slot_hook) ||
                !character_native::verify_sites({character_native::Site::animation_slot_evaluate})) { return false; }
            const auto base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
            slot_hook = std::make_unique<PLH::x64Detour>(base + character_native::rva(character_native::Site::animation_slot_evaluate),
                                                       reinterpret_cast<uintptr_t>(&evaluate_slot), &original_slot);
            if (!slot_hook->hook() || !original_slot)
            {
                release_native_detour(slot_hook);
                return false;
            }
            return true;
        }

        FName bone_name(UObject* mesh, int index)
        {
            Kh02ReflectedCall call(mesh, L"GetBoneName", "Kairi arm bones", 128);
            call.integer(L"BoneIndex", index);
            call.run();
            return *call.param(L"ReturnValue")->ContainerPtrToValuePtr<FName>(call.parameters.data());
        }

        auto* entries(UObject* set)
        {
            auto* property = set->GetPropertyByNameInChain(L"Anims");
            if (!property || !property->IsA<FArrayProperty>() ||
                static_cast<FArrayProperty*>(property)->GetInner()->GetElementSize() != sizeof(player_animation_support::AnimationEntry))
            {
                throw std::runtime_error("kairi_jump_animset_layout");
            }
            return property->ContainerPtrToValuePtr<TArray<player_animation_support::AnimationEntry>>(set);
        }

        float length(UObject* animation)
        {
            auto* property = animation->GetPropertyByNameInChain(L"SequenceLength");
            if (!property || property->GetElementSize() != sizeof(float)) { throw std::runtime_error("kairi_jump_sequence_layout"); }
            const auto value = *property->ContainerPtrToValuePtr<float>(animation);
            if (!std::isfinite(value) || value <= 0.0f) { throw std::runtime_error("kairi_jump_sequence_length"); }
            return value;
        }
    }

    bool prepare(UObject* pawn, UObject* anim, UObject* donor_set, UObject* run_donor_set)
    {
        if (const auto bound = current.load(); bound && bound->pawn.Get() == pawn && bound->anim.Get() == anim) { return true; }
        auto binding = std::make_shared<Binding>();
        binding->motions.resize(names.size());
        auto* mesh = object(pawn, L"MyMesh");
        if (!mesh || !install()) { return false; }
        binding->pawn = make_uobject_weak(pawn);
        binding->anim = make_uobject_weak(anim);
        binding->mesh = make_uobject_weak(mesh);
        for (auto* field : anim->GetClassPrivate()->ForEachPropertyInChain())
        {
            if (!field->IsA<FStructProperty>()) { continue; }
            auto* schema = static_cast<FStructProperty*>(field)->GetStruct();
            if (schema->GetName() != L"AnimNode_Slot") { continue; }
            auto* slot_name = find_struct_field_by_name(schema, L"SlotName");
            auto* node = field->ContainerPtrToValuePtr<std::byte>(anim);
            if (slot_name && *slot_name->ContainerPtrToValuePtr<FName>(node) == FName(L"FullBody", FNAME_Add))
            {
                const auto table = *reinterpret_cast<uintptr_t**>(node);
                const auto expected = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr)) +
                                      character_native::rva(character_native::Site::animation_slot_evaluate);
                if (table[3] != expected) { throw std::runtime_error("kairi_jump_slot_vtable"); }
                binding->slot_offset = field->GetOffset_Internal();
            }
        }
        if (!binding->slot_offset) { throw std::runtime_error("kairi_jump_slot_missing"); }
        Kh02ReflectedCall count(mesh, L"GetNumBones", "Kairi arm bones", 128);
        count.run();
        binding->bone_count = *count.param(L"ReturnValue")->ContainerPtrToValuePtr<int>(count.parameters.data());
        if (binding->bone_count <= 0 || binding->bone_count > max_bones) { throw std::runtime_error("kairi_jump_bone_count"); }
        std::map<std::wstring, bool> arm_names;
        std::map<std::wstring, bool> upper_names;
        std::map<std::wstring, bool> run_upper_names;
        for (int index = 0; index < binding->bone_count; ++index)
        {
            const auto name = bone_name(mesh, index).ToString();
            Kh02ReflectedCall parent(mesh, L"GetParentBone", "Kairi arm parent", 128);
            parent.name(L"BoneName", name.c_str());
            parent.run();
            const auto parent_name = parent.param(L"ReturnValue")->ContainerPtrToValuePtr<FName>(parent.parameters.data())->ToString();
            const bool arm = name == L"L_kata" || name == L"R_kata" || arm_names[parent_name];
            binding->arms[index] = arm;
            arm_names[name] = arm;
            binding->arm_count += arm;
            const bool upper = name == L"mune" || upper_names[parent_name];
            binding->upper_body[index] = upper;
            upper_names[name] = upper;
            binding->upper_body_count += upper;
            // Relax the run from the lower spine, leaving Kairi's pelvis and leg branches native.
            const bool run_upper = name == L"hara1" || run_upper_names[parent_name];
            binding->run_upper_body[index] = run_upper;
            run_upper_names[name] = run_upper;
            binding->run_upper_body_count += run_upper;
        }
        if (!arm_names[L"L_te"] || !arm_names[L"R_te"] || arm_names[L"L_ashi1"] || arm_names[L"R_ashi1"] ||
            arm_names[L"center"] || arm_names[L"mune"]) { throw std::runtime_error("kairi_jump_arm_mask"); }
        if (!upper_names[L"L_te"] || !upper_names[L"R_te"] || upper_names[L"L_ashi1"] ||
            upper_names[L"R_ashi1"] || upper_names[L"center"] || binding->upper_body_count <= binding->arm_count)
        { throw std::runtime_error("kairi_landing_upper_body_mask"); }
        if (!run_upper_names[L"L_te"] || !run_upper_names[L"R_te"] || !run_upper_names[L"mune"] ||
            run_upper_names[L"L_momo"] || run_upper_names[L"R_momo"] || run_upper_names[L"L_ashi1"] ||
            run_upper_names[L"R_ashi1"] || run_upper_names[L"center"] || run_upper_names[L"koshi"] ||
            binding->run_upper_body_count <= binding->upper_body_count)
        { throw std::runtime_error("kairi_run_upper_body_mask"); }
        auto* sets_field = anim->GetPropertyByNameInChain(L"MyAnimSets");
        if (!sets_field || !sets_field->IsA<FArrayProperty>() ||
            static_cast<FArrayProperty*>(sets_field)->GetInner()->GetElementSize() != sizeof(UObject*))
        { throw std::runtime_error("kairi_jump_sets_layout"); }
        auto* sets = sets_field->ContainerPtrToValuePtr<TArray<UObject*>>(anim);
        FStaticConstructObjectParameters parameters(donor_set->GetClassPrivate(), anim);
        parameters.SetFlags = RF_Transient;
        auto* retained = UObjectGlobals::StaticConstructObject(parameters);
        if (!is_uobject_valid_fast(retained)) { throw std::runtime_error("kairi_jump_reference_set"); }
        for (size_t index = 0; index < names.size(); ++index)
        {
            auto& motion = binding->motions[index];
            motion.normal = FName(names[index], FNAME_Add);
            motion.takeoff = motion.normal == FName(L"JumpHighStart", FNAME_Add);
            const bool roll = motion.normal == FName(L"RollLand", FNAME_Add);
            motion.landing = roll || motion.normal == FName(L"Land", FNAME_Add) ||
                motion.normal == FName(L"LightLand", FNAME_Add);
            motion.upper_body = motion.landing && !roll;
            // Keep the roll's arm tuck until its final recovery.
            motion.relaxed_start = roll ? 0.85f : 0.25f;
            for (auto* set : *sets)
            {
                if (!is_uobject_valid_fast(set)) { continue; }
                for (const auto& entry : *entries(set))
                {
                    if (entry.name == FName(L"Idle_GM", FNAME_Add) && is_uobject_valid_fast(entry.asset))
                    { binding->landing_idle = make_uobject_weak(entry.asset); }
                    if (entry.name == motion.normal && is_uobject_valid_fast(entry.asset) &&
                        entry.asset->GetPathName().starts_with(L"/Game/Character/pc/p_ex006/anm/rt/"))
                    { motion.source = make_uobject_weak(entry.asset); }
                }
            }
            // Takeoff approaches the arms-out rise pose, avoiding the authored
            // gripping frames at the beginning of Aqua's jump-start sequence.
            const auto donor_name = motion.takeoff ? FName(L"JumpHighRiseLoop", FNAME_Add) : motion.normal;
            for (const auto& entry : *entries(donor_set))
            {
                if (entry.name == donor_name && is_uobject_valid_fast(entry.asset))
                { motion.donor = make_uobject_weak(entry.asset); }
            }
            auto* source = motion.source.Get();
            auto* donor = motion.donor.Get();
            if (!source || !donor || source->GetClassPrivate()->GetName() != L"AnimSequence" ||
                donor->GetClassPrivate()->GetName() != L"AnimSequence") { throw std::runtime_error("kairi_jump_sequences_missing"); }
            motion.source_length = length(source);
            motion.donor_length = length(donor);
            player_animation_support::allow_animation_skeleton(anim, donor);
            entries(retained)->Add({FName((std::wstring(L"AP_KairiArms_") + names[index]).c_str(), FNAME_Add), donor});
        }
        auto* landing_idle = binding->landing_idle.Get();
        if (!landing_idle || landing_idle->GetClassPrivate()->GetName() != L"AnimSequence")
        { throw std::runtime_error("kairi_jump_landing_idle_missing"); }
        player_animation_support::allow_animation_skeleton(anim, landing_idle);
        entries(retained)->Add({FName(L"AP_KairiArms_LandingIdle", FNAME_Add), landing_idle});

        // Match native and donor directional samples by their authored blend
        // coordinates, never by array order. Source poses remain Kairi's;
        // extraction overlays the relaxed upper body at the same phase. Stops
        // retain the initial braking step, then recover before the combat tail.
        struct BlendSample { UObject* animation; std::array<float, 3> position; float rate; };
        static_assert(sizeof(BlendSample) == 24);
        auto find_run = [&](FName name, bool native) -> UObject*
        {
            if (!native)
            {
                for (const auto& entry : *entries(run_donor_set))
                { if (entry.name == name && is_uobject_valid_fast(entry.asset)) { return entry.asset; } }
                return nullptr;
            }
            for (auto* set : *sets)
            {
                if (!is_uobject_valid_fast(set)) { continue; }
                for (const auto& entry : *entries(set))
                {
                    if (entry.name == name && is_uobject_valid_fast(entry.asset) &&
                        entry.asset->GetPathName().starts_with(L"/Game/Character/pc/p_ex006/anm/rt/"))
                    { return entry.asset; }
                }
            }
            return nullptr;
        };
        auto add_run = [&](UObject* source, UObject* donor, bool stopping)
        {
            if (!is_uobject_valid_fast(source) || !is_uobject_valid_fast(donor) ||
                source->GetClassPrivate()->GetName() != L"AnimSequence" ||
                donor->GetClassPrivate()->GetName() != L"AnimSequence")
            { throw std::runtime_error("kairi_run_arm_sequence_missing"); }
            for (const auto& motion : binding->motions)
            { if (motion.source.Get() == source) { throw std::runtime_error("kairi_run_arm_duplicate_source"); } }
            Motion motion;
            motion.source = make_uobject_weak(source);
            motion.donor = make_uobject_weak(donor);
            motion.source_length = length(source);
            motion.donor_length = length(donor);
            motion.running = true;
            motion.stopping = stopping;
            player_animation_support::allow_animation_skeleton(anim, donor);
            entries(retained)->Add({FName((L"AP_KairiRunArms_" + std::to_wstring(binding->motions.size())).c_str(), FNAME_Add), donor});
            binding->motions.push_back(motion);
        };
        auto samples = [&](UObject* asset)
        {
            auto* field = asset->GetPropertyByNameInChain(L"SampleData");
            if (!field || !field->IsA<FArrayProperty>() ||
                static_cast<FArrayProperty*>(field)->GetInner()->GetElementSize() != sizeof(BlendSample))
            { throw std::runtime_error("kairi_run_arm_blendspace_layout"); }
            auto* values = field->ContainerPtrToValuePtr<TArray<BlendSample>>(asset);
            if (values->Num() <= 0 || values->Num() > 16) { throw std::runtime_error("kairi_run_arm_sample_count"); }
            return values;
        };
        for (const auto* stem : {L"RunStart", L"RunLoop", L"RunEndL", L"RunEndR"})
        {
            auto* source = find_run(FName((std::wstring(stem) + L"B").c_str(), FNAME_Add), true);
            const bool stopping = std::wstring(stem).starts_with(L"RunEnd");
            const auto suffix = stopping ? L"" : L"N";
            auto* donor = find_run(FName((std::wstring(stem) + suffix).c_str(), FNAME_Add), false);
            if (!source || !donor) { throw std::runtime_error("kairi_run_arm_binding_missing"); }
            if (source->GetClassPrivate()->GetName() == L"AnimSequence") { add_run(source, donor, stopping); continue; }
            if (source->GetClassPrivate()->GetName() != L"BlendSpace1D" || donor->GetClassPrivate()->GetName() != L"BlendSpace1D")
            { throw std::runtime_error("kairi_run_arm_blendspace_type"); }
            for (const auto& source_sample : *samples(source))
            {
                UObject* matched{};
                for (const auto& donor_sample : *samples(donor))
                {
                    if (source_sample.position == donor_sample.position)
                    {
                        if (matched) { throw std::runtime_error("kairi_run_arm_ambiguous_sample"); }
                        matched = donor_sample.animation;
                    }
                }
                add_run(source_sample.animation, matched, stopping);
            }
        }
        // Private aliases retain donors without overriding any native binding.
        // Pose extraction cannot emit donor voices or gameplay notifications.
        sets->Add(retained);
        current_slot.store(reinterpret_cast<std::byte*>(anim) + binding->slot_offset, std::memory_order_relaxed);
        current.store(std::move(binding), std::memory_order_release);
        return true;
    }

    void playback(UObject* pawn, FName name, FName slot, bool unarmed)
    {
        const auto binding = current.load(std::memory_order_acquire);
        if (!binding || binding->pawn.Get() != pawn || slot != FName(L"FullBody", FNAME_Add)) { return; }
        binding->unarmed.store(unarmed, std::memory_order_relaxed);
    }

    void visibility(UObject* pawn, bool unarmed)
    {
        const auto binding = current.load(std::memory_order_acquire);
        if (binding && binding->pawn.Get() == pawn) { binding->unarmed.store(unarmed, std::memory_order_relaxed); }
    }
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
    nlohmann::json snapshot()
    {
        const auto binding = current.load();
        if (!binding) { return {{"prepared", false}}; }
        auto clips = nlohmann::json::object();
        auto donors = nlohmann::json::object();
        for (size_t index = 0; index < names.size(); ++index)
        {
            const std::wstring name(names[index]);
            clips[std::string(name.begin(), name.end())] = binding->samples[index].load();
            if (auto* donor = binding->motions[index].donor.Get())
            {
                const auto path = donor->GetPathName();
                donors[std::string(name.begin(), name.end())] = std::string(path.begin(), path.end());
            }
        }
        return {{"prepared", true}, {"unarmed", binding->unarmed.load()}, {"jumping", binding->jumping.load()},
                {"arm_bones", binding->arm_count}, {"blended", binding->blended.load()},
                {"landing_upper_body_bones", binding->upper_body_count},
                {"run_upper_body_bones", binding->run_upper_body_count},
                {"idle_captures", binding->idle_captures.load()}, {"idle_capture_time", binding->idle_capture_time.load()},
                {"run_pairs", binding->motions.size() - names.size()}, {"run_blended", binding->run_blended.load()},
                {"run_armed_samples", binding->run_armed_samples.load()},
                {"run_stop_blended", binding->run_stop_blended.load()}, {"run_stop_recovered", binding->run_stop_recovered.load()},
                {"evaluations", binding->evaluations.load()},
                {"rejected", binding->rejected.load()}, {"armed_samples", binding->armed_samples.load()},
                {"clips", clips}, {"donors", donors}};
    }
#endif
}
