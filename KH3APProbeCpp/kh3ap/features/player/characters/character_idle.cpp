#include "kh3ap/core/objects/actor_owning_world.h"
#include "kh3ap/features/player/characters/character_profile.h"
#include <polyhook2/Detour/x64Detour.hpp>
#include "kh3ap/features/player/characters/character_idle.h"
#include "kh3ap/features/player/characters/character_cinematic_proportions.h"
#include "kh3ap/features/player/characters/character_pose_handoff.h"
#include "kh3ap/features/player/player_animation_support.h"
#include "kh3ap/features/player/movement/kairi_jump_arms.h"
#include "kh3ap/core/performance_build.h"
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
#include "kh3ap/diagnostics/character_swap_trace.h"
#endif
#include "kh3ap/core/objects/asset_load_helpers.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/core/objects/uobject_weak.h"
#include "kh3ap/features/player/player_ids.h"
#include "kh3ap/features/player/player_state_ids.h"
#include "kh3ap/features/player/characters/character_swap.h"
#include "kh3ap/core/reflection/reflected_function_call.h"
#include "kh3ap/features/kh02/runtime/kh02_world_context.h"
#include <Unreal/UObject.hpp>
#include <Unreal/UClass.hpp>
#include <Unreal/FProperty.hpp>
#include <Unreal/NameTypes.hpp>
#include <Unreal/TArray.hpp>
#include <Unreal/Property/FArrayProperty.hpp>
#include <Unreal/Property/FStructProperty.hpp>
#include <Unreal/UScriptStruct.hpp>
#include <Unreal/UObjectGlobals.hpp>
#include <DynamicOutput/DynamicOutput.hpp>
#define NOMINMAX
#include <Windows.h>
#include <cstring>
#include <algorithm>
#include <array>
#include <chrono>
#include <memory>
#include <optional>
#include <stdexcept>

namespace kh3ap::character_idle
{
    using namespace RC::Unreal;

    namespace
    {
        constexpr auto max_animation_parameter_bytes = 256;

        constexpr auto kairi_idle_animset_load_uuid = 0x4b48338a;
        constexpr auto kairi_idle_animset_load_timeout = std::chrono::seconds(3);
        constexpr auto idle_blend_in_seconds = 0.2f;
        constexpr auto kairi_weapon_visible_vtable_offset = 0x1330;
        constexpr auto battle_mode_mask = 2u;
        constexpr size_t pawn_battle_mode_flags_offset = 0x630;
        constexpr size_t state_owner_pawn_offset = 0x68;

        constexpr auto set_path = player_animation_support::kairi_idle_animation_set_path;
        constexpr auto last_pose_notify_path = L"/Script/TresGame.Default__TresAnimNotify_LastAnimPose";
        const uintptr_t resolve_rva = character_native::rva(character_native::Site::idle_resolve);
        const uintptr_t battle_rva = character_native::rva(character_native::Site::set_battle);
        const uintptr_t play_rva = character_native::rva(character_native::Site::idle_play);
        const uintptr_t dismiss_rva = character_native::rva(character_native::Site::dismiss_weapon);
        const uintptr_t weapon_rva = character_native::rva(character_native::Site::set_weapon);
        const uintptr_t idle_update_rva =
            character_native::rva(character_native::Site::idle_update);
        constexpr float native_idle_delay_seconds = 3.0f;
        constexpr float relaxed_blend_seconds = 0.35f;
        using Resolve = void* (*)(UObject*, FName);
        using IdleUpdate = void (*)(UObject*, float);
        using SetBattle = void (*)(UObject*, bool);
        using DismissWeapon = void (*)(UObject*, bool, float);
        using SetWeapon = void (*)(UObject*, bool, bool, float);
        using Play = void* (*)(UObject*, FName, FName, float, uint32_t, float, float, float);
        uint64_t original_resolve{}, original_battle{}, original_play{}, original_dismiss{},
            original_weapon{}, original_idle_update{};
        std::unique_ptr<PLH::x64Detour> resolve_hook, battle_hook, play_hook, dismiss_hook,
            weapon_hook, idle_update_hook;
        FWeakObjectPtr kairi_owner, kairi_instance, kairi_requested_owner;
        struct SneezeFinisher
        {
            FWeakObjectPtr animation, queue, state;
            FName motion;
            std::chrono::steady_clock::time_point expires;
        };
        std::optional<SneezeFinisher> sneeze_finisher;
        FWeakObjectPtr sneeze_mesh;
        character_cinematic_proportions::Lease sneeze_proportions;
        FWeakObjectPtr kairi_queue, kairi_pose_notify, kairi_idle_asset;
        struct RunPoseCompletion
        {
            FWeakObjectPtr notify;
            FWeakObjectPtr animation;
        };
        RunPoseCompletion kairi_run_pose, roxas_run_pose;
        size_t pose_notify_slot{};
        int kairi_current_state_offset{}, kairi_state_id_offset{};
        FWeakObjectPtr roxas_owner, roxas_instance, roxas_queue;
        int roxas_current_state_offset{}, roxas_state_id_offset{};
        FName roxas_battle_idle, roxas_relaxed_idle, roxas_weapon_break, roxas_relaxed_break;
        bool roxas_last_battle{};
        FName kairi_battle_idle, kairi_relaxed_idle, kairi_battle_end;
        FName kairi_idle_break, kairi_weapon_break, kairi_aqua_idle_break;
        bool disabled{};
        bool kairi_relaxed{};
        float kairi_remaining_idle_seconds{};
        std::chrono::steady_clock::time_point kairi_load_deadline{};

        struct RelaxedMovement
        {
            const wchar_t* stem;
            const wchar_t* donor;
            const wchar_t* alias;
            std::array<FName, 4> requests;
            FName prepared;
        };
        // Keep Kairi's native run start/loop/stops and layer relaxed arms only.
        // Aqua supplies walking and the pose-completion notify.
        std::array kairi_movement{
            RelaxedMovement{L"RunStart", L"RunStartN", L"AP_KairiRunStartN"},
            RelaxedMovement{L"RunLoop", L"RunLoopN", L"AP_KairiRunLoopN"},
            RelaxedMovement{L"RunEndL", L"RunEndL", L"AP_KairiRunEndLN"},
            RelaxedMovement{L"RunEndR", L"RunEndR", L"AP_KairiRunEndRN"},
            RelaxedMovement{L"WalkLoop", L"WalkLoopN", L"AP_KairiWalkLoopN"},
            RelaxedMovement{L"WalkEndL", L"WalkEndL", L"AP_KairiWalkEndLN"},
            RelaxedMovement{L"WalkEndR", L"WalkEndR", L"AP_KairiWalkEndRN"},
            // Native Run requests this after a vault or landing changes the
            // pose latch. Aqua's equivalent retains its completion notifies.
            RelaxedMovement{L"RunBattleStart", L"RunBattleStart", L"AP_KairiRunBattleStart"}};

        FName relaxed_movement(FName name)
        {
            for (const auto& motion : kairi_movement)
            {
                if (std::find(motion.requests.begin(), motion.requests.end(), name) != motion.requests.end())
                {
                    return motion.prepared;
                }
            }
            return name;
        }

        void use_reference_translations(UObject* animation)
        {
            auto* skeleton = try_read_reflected_object_property_value(animation, L"Skeleton");
            auto* bones = is_uobject_valid_fast(skeleton) ? skeleton->GetPropertyByNameInChain(L"BoneTree") : nullptr;
            auto* inner = bones && bones->IsA<FArrayProperty>() ? static_cast<FArrayProperty*>(bones)->GetInner() : nullptr;
            auto* schema = inner && inner->IsA<FStructProperty>() ? static_cast<FStructProperty*>(inner)->GetStruct() : nullptr;
            auto* mode = is_uobject_valid_fast(schema) ? find_struct_field_by_name(schema, L"TranslationRetargetingMode") : nullptr;
            struct BoneNode { FName name; int32_t parent; uint8_t translation_mode; uint8_t padding[3]; };
            static_assert(sizeof(BoneNode) == 16);
            if (!inner || inner->GetElementSize() != sizeof(BoneNode) || !mode ||
                mode->GetElementSize() != 1 || mode->GetOffset_Internal() != offsetof(BoneNode, translation_mode))
            {
                throw std::runtime_error("kairi_movement_retarget_layout");
            }
            // Use the receiving mesh's reference translations, without dividing
            // by source reference lengths. Keep root motion authored.
            auto& tree = *bones->ContainerPtrToValuePtr<TArray<BoneNode>>(skeleton);
            for (int index = 1; index < tree.Num(); ++index)
            {
                tree[index].translation_mode = 1; // EBoneTranslationRetargetingMode::Skeleton
            }

        }

        RunPoseCompletion run_pose_completion(UObject* animation)
        {
            auto* notifies = is_uobject_valid_fast(animation) ? animation->GetPropertyByNameInChain(L"Notifies") : nullptr;
            auto* inner = notifies && notifies->IsA<FArrayProperty>()
                              ? static_cast<FArrayProperty*>(notifies)->GetInner() : nullptr;
            auto* schema = inner && inner->IsA<FStructProperty>()
                               ? static_cast<FStructProperty*>(inner)->GetStruct() : nullptr;
            auto* notify_field = schema ? find_struct_field_by_name(schema, L"Notify") : nullptr;
            if (!notify_field || notify_field->GetClass().GetName() != L"ObjectProperty" ||
                notify_field->GetElementSize() != sizeof(UObject*))
            {
                throw std::runtime_error("run_pose_notify_layout");
            }
            auto* events = notifies->ContainerPtrToValuePtr<FScriptArray>(animation);
            if (events->Num() < 0 || events->Num() > 256)
            {
                throw std::runtime_error("run_pose_notify_count");
            }
            for (int index = 0; index < events->Num(); ++index)
            {
                auto* event = static_cast<std::byte*>(events->GetData()) + index * inner->GetElementSize();
                auto* notify = *notify_field->ContainerPtrToValuePtr<UObject*>(event);
                if (!is_uobject_valid_fast(notify) || notify->GetOuterPrivate() != animation ||
                    notify->GetClassPrivate()->GetName() != L"TresAnimNotify_LastAnimPose") { continue; }
                auto* pose = notify->GetPropertyByNameInChain(L"m_LastAnimPose");
                if (pose && pose->GetElementSize() == 1 && *pose->ContainerPtrToValuePtr<uint8_t>(notify) == 1)
                {
                    return {make_uobject_weak(notify), make_uobject_weak(animation)};
                }
            }
            throw std::runtime_error("run_pose_notify_missing");
        }

        void prepare_kairi_movement(UObject* anim, UObject* source, TArray<UObject*>& sets)
        {
            using player_animation_support::AnimationEntry;
            auto* field = source->GetPropertyByNameInChain(L"Anims");
            if (!field || !field->IsA<FArrayProperty>() ||
                static_cast<FArrayProperty*>(field)->GetInner()->GetElementSize() != sizeof(AnimationEntry) ||
                !player_animation_support::supported_skeleton_layout())
            {
                throw std::runtime_error("kairi_movement_animation_layout");
            }
            auto* entries = field->ContainerPtrToValuePtr<TArray<AnimationEntry>>(source);
            std::array<AnimationEntry, std::tuple_size_v<decltype(kairi_movement)> + 1> borrowed{};
            for (size_t index = 0; index < kairi_movement.size(); ++index)
            {
                auto& motion = kairi_movement[index];
                motion.prepared = FName(motion.alias, FNAME_Add);
                const std::array suffixes{L"", L"N", L"B", L"W"};
                for (size_t stance = 0; stance < suffixes.size(); ++stance)
                {
                    motion.requests[stance] = FName((std::wstring(motion.stem) + suffixes[stance]).c_str(), FNAME_Add);
                }
                const FName donor(motion.donor, FNAME_Add);
                const bool native_run = index < 4;
                if (native_run)
                {
                    const FName native_name((std::wstring(motion.stem) + L"B").c_str(), FNAME_Add);
                    for (auto* set : sets)
                    {
                        if (!is_uobject_valid_fast(set)) { continue; }
                        auto* native_field = set->GetPropertyByNameInChain(L"Anims");
                        if (!native_field || !native_field->IsA<FArrayProperty>() ||
                            static_cast<FArrayProperty*>(native_field)->GetInner()->GetElementSize() != sizeof(AnimationEntry))
                        { continue; }
                        for (const auto& entry : *native_field->ContainerPtrToValuePtr<TArray<AnimationEntry>>(set))
                        {
                            if (entry.name == native_name && is_uobject_valid_fast(entry.asset) &&
                                entry.asset->GetPathName().starts_with(L"/Game/Character/pc/p_ex006/anm/rt/"))
                            { borrowed[index] = {motion.prepared, entry.asset}; }
                        }
                    }
                }
                else
                {
                    for (const auto& entry : *entries)
                    {
                        if (entry.name == donor) { borrowed[index] = {motion.prepared, entry.asset}; break; }
                    }
                }
                if (!is_uobject_valid_fast(borrowed[index].asset))
                {
                    throw std::runtime_error("kairi_movement_animation_missing");
                }
                player_animation_support::allow_animation_skeleton(anim, borrowed[index].asset);
                if (!native_run) { use_reference_translations(borrowed[index].asset); }
            }
            kairi_run_pose = run_pose_completion(borrowed[kairi_movement.size() - 1].asset);
            kairi_idle_break = FName(L"IdleBreakN", FNAME_Add);
            kairi_weapon_break = FName(L"IdleBreakW", FNAME_Add);
            kairi_aqua_idle_break = FName(L"AP_KairiIdleBreakN", FNAME_Add);
            for (const auto& entry : *entries)
            {
                if (entry.name == kairi_idle_break)
                {
                    borrowed.back() = {kairi_aqua_idle_break, entry.asset};
                    break;
                }
            }
            if (!is_uobject_valid_fast(borrowed.back().asset))
            {
                throw std::runtime_error("kairi_idle_break_animation_missing");
            }
            player_animation_support::allow_animation_skeleton(anim, borrowed.back().asset);
            use_reference_translations(borrowed.back().asset);
            FStaticConstructObjectParameters parameters(source->GetClassPrivate(), anim);
            parameters.SetFlags = RF_Transient;
            auto* replacement = UObjectGlobals::StaticConstructObject(parameters);
            if (!is_uobject_valid_fast(replacement))
            {
                throw std::runtime_error("kairi_movement_animset_create");
            }
            auto* copied = field->ContainerPtrToValuePtr<TArray<AnimationEntry>>(replacement);
            for (const auto& entry : borrowed) { copied->Add(entry); }
            // Unique aliases avoid replacing the native combat entries or
            // traversal fallbacks. MyAnimSets keeps these assets alive.
            sets.Add(replacement);
        }

        void set_weapon(UObject* pawn, bool visible, bool effect, float rate);

        bool kairi_is_idle_or_running()
        {
            auto* queue = kairi_queue.Get();
            if (!queue) { return false; }
            UObject* state{};
            std::memcpy(&state, reinterpret_cast<std::byte*>(queue) + kairi_current_state_offset, sizeof(state));
            if (!is_uobject_valid_fast(state)) { return false; }
            const auto id = *(reinterpret_cast<uint8_t*>(state) + kairi_state_id_offset);
            return id == player_state_ids::idle || id == player_state_ids::run;
        }

        UObject* object(UObject* value, const wchar_t* field)
        {
            return is_uobject_valid_fast(value)
                       ? try_read_reflected_object_property_value(value, field)
                       : nullptr;
        }

        void complete_run_pose(UObject* pawn, const RunPoseCompletion& pose)
        {
            auto* notify = pose.notify.Get();
            auto* animation = pose.animation.Get();
            auto* mesh = object(pawn, L"MyMesh");
            if (notify && animation && mesh)
            {
                const auto table = *reinterpret_cast<uintptr_t**>(notify);
                reinterpret_cast<void (*)(UObject*, UObject*, UObject*)>(table[pose_notify_slot])(notify, mesh, animation);
            }
        }

        void prepare_pose_notify_slot()
        {
            auto* voice = find_loaded_runtime_asset(L"/Script/TresGame.Default__TresAnimNotify_PlayVoice");
            if (!is_uobject_valid_fast(voice) || voice->GetClassPrivate()->GetName() != L"TresAnimNotify_PlayVoice")
            {
                throw std::runtime_error("pose_notify_slot_unavailable");
            }
            // Locate UAnimNotify::Notify through the already verified PlayVoice
            // implementation. Both classes inherit the same virtual slot; do not
            // assume a platform-specific slot or write private pawn pose flags.
            const auto address = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr)) +
                character_native::rva(character_native::Site::voice_notify);
            const auto table = *reinterpret_cast<uintptr_t**>(voice);
            size_t matches{};
            size_t matched_slot{};
            for (size_t index = 0; index < 128; ++index)
            {
                if (table[index] == address) { matched_slot = index; ++matches; }
            }
            if (matches != 1)
            {
                throw std::runtime_error("pose_notify_slot_unavailable");
            }
            pose_notify_slot = matched_slot;
        }

        void prepare_roxas_run_pose()
        {
            roxas_run_pose.animation = {};
            try
            {
                auto* animation = find_loaded_runtime_asset(
                    L"/Game/Character/pc/p_ex007/anm/rt/body/RunStartB.RunStartB");
                if (!is_uobject_valid_fast(animation))
                {
                    throw std::runtime_error("roxas_run_start_missing");
                }
                prepare_pose_notify_slot();
                if (!roxas_run_pose.notify.Get())
                {
                    auto* defaults = find_loaded_runtime_asset(last_pose_notify_path);
                    auto* pose = is_uobject_valid_fast(defaults)
                                     ? defaults->GetPropertyByNameInChain(L"m_LastAnimPose")
                                     : nullptr;
                    auto* engine = UObjectGlobals::StaticFindObject<UObject*>(
                        nullptr, nullptr, L"/Engine/Transient.TresGameEngine_0");
                    auto* package = is_uobject_valid_fast(engine) ? engine->GetOuterPrivate() : nullptr;
                    if (!pose || pose->GetElementSize() != 1 ||
                        defaults->GetClassPrivate()->GetName() != L"TresAnimNotify_LastAnimPose" ||
                        !is_uobject_valid_fast(package) || package->GetClassPrivate()->GetName() != L"Package")
                    {
                        throw std::runtime_error("roxas_run_pose_notify_layout");
                    }

                    FStaticConstructObjectParameters parameters(defaults->GetClassPrivate(), package);
                    parameters.Name = FName(L"KH3AP_RoxasRunPoseNotify", FNAME_Add);
                    parameters.SetFlags = RF_Transient;
                    auto* notify = UObjectGlobals::StaticConstructObject(parameters);
                    if (!is_uobject_valid_fast(notify))
                    {
                        throw std::runtime_error("roxas_run_pose_notify_create");
                    }
                    constexpr uint8_t battle_run_pose = 1;
                    *pose->ContainerPtrToValuePtr<uint8_t>(notify) = battle_run_pose;
                    // Keep one transient notify for the process. Its package
                    // outer avoids retaining a pawn or world through the root.
                    notify->SetRootSet();
                    roxas_run_pose.notify = make_uobject_weak(notify);
                }
                roxas_run_pose.animation = make_uobject_weak(animation);
            }
            catch (const std::runtime_error& failure)
            {
                RC::Output::send<RC::LogLevel::Warning>(
                    STR("[KH3APProbeCpp] Roxas run pose completion unavailable: {}\n"),
                    std::wstring(failure.what(), failure.what() + std::strlen(failure.what())));
            }
        }

        void prepare_kairi_pose_completion(UObject* idle_set)
        {
            auto* notify = find_loaded_runtime_asset(last_pose_notify_path);
            auto* pose = is_uobject_valid_fast(notify) ? notify->GetPropertyByNameInChain(L"m_LastAnimPose") : nullptr;
            if (!pose || pose->GetElementSize() != 1 || *pose->ContainerPtrToValuePtr<uint8_t>(notify) != 0 ||
                notify->GetClassPrivate()->GetName() != L"TresAnimNotify_LastAnimPose")
            {
                throw std::runtime_error("kairi_idle_pose_notify_layout");
            }
            prepare_pose_notify_slot();
            auto* field = idle_set->GetPropertyByNameInChain(L"Anims");
            if (!field || !field->IsA<FArrayProperty>() ||
                static_cast<FArrayProperty*>(field)->GetInner()->GetElementSize() != sizeof(player_animation_support::AnimationEntry))
            {
                throw std::runtime_error("kairi_idle_pose_notify_unavailable");
            }
            for (const auto& entry : *field->ContainerPtrToValuePtr<TArray<player_animation_support::AnimationEntry>>(idle_set))
            {
                if (entry.name == kairi_relaxed_idle && is_uobject_valid_fast(entry.asset))
                {
                    kairi_pose_notify = make_uobject_weak(notify);
                    kairi_idle_asset = make_uobject_weak(entry.asset);
                    return;
                }
            }
            throw std::runtime_error("kairi_idle_pose_asset_missing");
        }

        void prepare_roxas_face(UObject* anim)
        {
            using player_animation_support::AnimationEntry;
            auto* field = anim->GetPropertyByNameInChain(L"m_SubAnimSets");
            if (!field || !field->IsA<FArrayProperty>() ||
                static_cast<FArrayProperty*>(field)->GetInner()->GetElementSize() != sizeof(UObject*))
            {
                throw std::runtime_error("roxas_face_sets_layout");
            }
            const FName neutral(L"NO_1_EL", FNAME_Add);
            const FName open(L"BA_0_EL", FNAME_Add);
            auto* sets = field->ContainerPtrToValuePtr<TArray<UObject*>>(anim);
            for (auto& set : *sets)
            {
                auto* entries = is_uobject_valid_fast(set) ? set->GetPropertyByNameInChain(L"Anims") : nullptr;
                if (!entries || !entries->IsA<FArrayProperty>() ||
                    static_cast<FArrayProperty*>(entries)->GetInner()->GetElementSize() != sizeof(AnimationEntry))
                {
                    throw std::runtime_error("roxas_face_entries_layout");
                }
                auto* authored = entries->ContainerPtrToValuePtr<TArray<AnimationEntry>>(set);
                UObject* neutral_asset{};
                UObject* open_asset{};
                for (const auto& entry : *authored)
                {
                    if (entry.name == neutral) { neutral_asset = entry.asset; }
                    if (entry.name == open) { open_asset = entry.asset; }
                }
                if (!is_uobject_valid_fast(neutral_asset) || !is_uobject_valid_fast(open_asset)) { continue; }
                // NO_1_EL is Roxas's two-frame closed-eye pose (0001EM).
                // Use his own animated open-eye loop (0100EM) for the neutral
                // request. Clone the set so NPC Roxas and other instances keep
                // their authored assets; retain every other facial expression.
                if (neutral_asset == open_asset) { return; }
                FStaticConstructObjectParameters parameters(set->GetClassPrivate(), anim);
                parameters.SetFlags = RF_Transient;
                auto* replacement = UObjectGlobals::StaticConstructObject(parameters);
                if (!is_uobject_valid_fast(replacement))
                {
                    throw std::runtime_error("roxas_face_set_create");
                }
                auto* copied = entries->ContainerPtrToValuePtr<TArray<AnimationEntry>>(replacement);
                for (const auto& entry : *authored)
                {
                    copied->Add({entry.name, entry.name == neutral ? open_asset : entry.asset});
                }
                set = replacement; // Reflected strong reference owned by this anim instance.
                return;
            }
            throw std::runtime_error("roxas_face_animation_missing");
        }

        bool in_battle(UObject* pawn)
        {
            // Verified EGS IsBattleMode getter: (DWORD[pawn + 0x630] >> 1) & 1.
            // No reflected calls or discovery in the animation lookup path.
            uint32_t flags{};
            std::memcpy(&flags, reinterpret_cast<std::byte*>(pawn) + pawn_battle_mode_flags_offset,
                        sizeof(flags));
            return (flags & battle_mode_mask) != 0;
        }

        bool roxas_is_idle()
        {
            auto* queue = roxas_queue.Get();
            if (!queue) { return false; }
            UObject* state{};
            std::memcpy(&state, reinterpret_cast<std::byte*>(queue) + roxas_current_state_offset, sizeof(state));
            return is_uobject_valid_fast(state) &&
                *(reinterpret_cast<uint8_t*>(state) + roxas_state_id_offset) == player_state_ids::idle;
        }

        bool kairi_weapon_visible(UObject* pawn)
        {
            const auto table = *reinterpret_cast<uintptr_t**>(pawn);
            return reinterpret_cast<bool (*)(UObject*)>(table[kairi_weapon_visible_vtable_offset / sizeof(uintptr_t)])(pawn);
        }

        void* resolve(UObject* anim, FName name)
        {
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
            const character_swap_trace::NativeScope trace{"animation_resolve"};
#endif
            auto original = reinterpret_cast<Resolve>(original_resolve);
            const auto effective_name = effective_animation_name(anim, name);
            if (effective_name != name)
            {
                if (auto* entry = original(anim, effective_name))
                {
                    return entry;
                }
            }
            return original(anim, name);
        }

        void* play(UObject* anim, FName name, FName slot, float blend, uint32_t flags, float rate,
                   float scale_xy, float scale_z)
        {
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
            const character_swap_trace::NativeScope trace{"animation_play"};
#endif
            if (anim == roxas_instance.Get())
            {
                static const FName run_start_normal(L"RunStartN", FNAME_Add);
                static const FName run_start_weapon(L"RunStartW", FNAME_Add);
                static const FName run_start_battle(L"RunStartB", FNAME_Add);
                if (name == run_start_normal || name == run_start_weapon || name == run_start_battle)
                {
                    auto* pawn = roxas_owner.Get();
                    if (pawn && character_swap::owns_swap_pawn(pawn) && !in_battle(pawn))
                    {
                        // All three starts use Roxas's battle stride. Mark that pose
                        // on entry so Run goes straight to his loop without playing
                        // Sora's RunBattleStartW to complete the stance change.
                        complete_run_pose(pawn, roxas_run_pose);
                    }
                }
                const auto selected = effective_animation_name(anim, name);
                if (selected != name)
                {
                    name = selected;
                    blend = std::max(blend, relaxed_blend_seconds);
                }
            }
            if (anim == kairi_instance.Get() && relaxed_movement(name) != name)
            {
                auto* pawn = kairi_owner.Get();
                if (pawn && character_swap::owns_swap_pawn(pawn) && !in_battle(pawn))
                {
                    // Kairi's native Run entry can retain a drawn weapon even
                    // without battle mode. Dismiss on the movement event, not
                    // every frame; attacks still use the native visibility path.
                    if (!kairi_relaxed) { set_weapon(pawn, false, true, 1.0f); }
                    const auto movement = relaxed_movement(name);
                    if (movement == kairi_movement[0].prepared || movement == kairi_movement[1].prepared)
                    {
                        // Native Run asks for RunBattleStart when the last pose
                        // differs from its expected running pose, even outside
                        // battle. Complete that authored pose notification on
                        // the normal start/loop event so the next Run update
                        // keeps moving without inserting the weapon gesture.
                        // This changes the pose latch, not battle mode.
                        complete_run_pose(pawn, kairi_run_pose);
                    }
                    // Preserve the native playback name. Run compares it on
                    // subsequent updates; an alias here restarts the motion
                    // every frame and leaves it stuck near its first pose.
                    // Only resolve() substitutes the underlying animation.
                }
            }
            rate *= movement_play_rate_scale(anim, name);
            if ((name == kairi_battle_idle || name == kairi_battle_end ||
                 name == kairi_idle_break || name == kairi_weapon_break) &&
                anim == kairi_instance.Get())
            {
                auto* pawn = kairi_owner.Get();
                if (pawn && kairi_relaxed && !in_battle(pawn))
                {
                    // Native idle entry can request an immediate pose change.
                    // Blend from the current pose; leave outgoing attacks and
                    // movement requests at their native timing.
                    blend = std::max(blend, relaxed_blend_seconds);
                    if (name == kairi_battle_end && character_swap::owns_swap_pawn(pawn))
                    {
                        auto* notify = kairi_pose_notify.Get();
                        auto* asset = kairi_idle_asset.Get();
                        auto* mesh = object(pawn, L"MyMesh");
                        if (notify && asset && mesh)
                        {
                            // The replaced BattleEnd contains LastAnimPose(Normal).
                            // Deliver that native completion event without its
                            // weapon gesture, or Idle requests dismissal forever.
                            const auto table = *reinterpret_cast<uintptr_t**>(notify);
                            reinterpret_cast<void (*)(UObject*, UObject*, UObject*)>(table[pose_notify_slot])(
                                notify, mesh, asset);
                        }
                    }
                }
            }
            // This hook already owns the shared animation-play entry. During
            // a character handoff no incoming animation may blend from the
            // parked pose, including the relaxed-idle blend selected above.
            if (character_pose_handoff::active_for(anim))
            {
                blend = 0.0f;
            }
            if (anim == kairi_instance.Get())
            {
                if (auto* pawn = kairi_owner.Get())
                {
                    kairi_jump_arms::playback(pawn, name, slot,
                        character_swap::owns_swap_pawn(pawn) && !in_battle(pawn) && !kairi_weapon_visible(pawn));
                }
            }
            return reinterpret_cast<Play>(original_play)(anim, name, slot, blend, flags, rate,
                                                         scale_xy, scale_z);
        }

        void refresh_idle(UObject* pawn)
        {
            auto* state = object(object(pawn, L"MyStateComp"), L"m_pCurrentState");
            if (!is_uobject_valid_fast(state) ||
                state->GetClassPrivate()->GetName() != L"TresPlayerState_Idle")
            {
                return;
            }
            ReflectedFunctionCall play(pawn, L"BP_AnimPlayAnimset", "Character exploration idle",
                                   max_animation_parameter_bytes);
            play.name(L"InAnimName", L"IdleB");
            play.name(L"InSlotNodeName", L"FullBody");
            play.number(L"InBlendInTime", idle_blend_in_seconds);
            play.boolean(L"InLoop", true);
            play.number(L"InPlayRate", 1.0f);
            play.boolean(L"InRootTrans", false);
            play.number(L"InRootTransScaleXY", 1.0f);
            play.number(L"InRootTransScaleZ", 1.0f);
            play.run();
        }

        void set_weapon(UObject* pawn, bool visible, bool effect, float rate)
        {
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
            const character_swap_trace::NativeScope trace{"weapon_visibility"};
#endif
            if (pawn == kairi_owner.Get() && kairi_instance.Get())
            {
                if (visible && kairi_relaxed && !in_battle(pawn) &&
                    character_swap::owns_swap_pawn(pawn) && kairi_is_idle_or_running())
                {
                    // Idle entry requests a drawn weapon even after relaxed
                    // locomotion. Preserve that handoff instead of flashing
                    // IdleB and restarting the three-second dismissal delay.
                    // Attack states and battle entry still draw normally.
                    visible = false;
                    effect = false;
                }
                // Attacks and idle entry draw the weapon through this native
                // event. Keep the combat idle until the native dismissal event.
                kairi_relaxed = !visible && !in_battle(pawn);
                kairi_remaining_idle_seconds = visible ? native_idle_delay_seconds : 0.0f;
            }
            reinterpret_cast<SetWeapon>(original_weapon)(pawn, visible, effect, rate);
            if (pawn == kairi_owner.Get())
            {
                kairi_jump_arms::visibility(pawn, !in_battle(pawn) && !kairi_weapon_visible(pawn));
            }
        }

        void dismiss_weapon(UObject* pawn, bool effect, float rate)
        {
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
            const character_swap_trace::NativeScope trace{"weapon_dismiss"};
#endif
            if (pawn == roxas_owner.Get() && roxas_instance.Get() &&
                character_swap::owns_swap_pawn(pawn) && roxas_is_idle() && !in_battle(pawn))
            {
                // His dual-weapon type is excluded by the shared dismissal
                // filter. Complete the ordinary visibility event so Idle does
                // not repeat BattleEnd and return to a drawn combat pose.
                reinterpret_cast<SetWeapon>(original_weapon)(pawn, false, effect, rate);
                return;
            }
            if (pawn == kairi_owner.Get() && kairi_instance.Get() && !in_battle(pawn))
            {
                // TresPlayerState_Idle owns Sora's three-second countdown and
                // calls this event when it expires. Alternate weapons can be
                // excluded by the native weapon-type filter; use its ordinary
                // visibility setter directly for this prepared Kairi only.
                if (kairi_remaining_idle_seconds <= 0.0f)
                {
                    set_weapon(pawn, false, effect, rate);
                }
                return;
            }
            reinterpret_cast<DismissWeapon>(original_dismiss)(pawn, effect, rate);
        }

        void idle_update(UObject* state, float delta_seconds)
        {
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
            const character_swap_trace::NativeScope trace{"idle_update"};
#endif
            reinterpret_cast<IdleUpdate>(original_idle_update)(state, delta_seconds);
            if (auto* pawn = roxas_owner.Get(); pawn && roxas_instance.Get())
            {
                UObject* state_pawn{};
                std::memcpy(&state_pawn, reinterpret_cast<std::byte*>(state) + state_owner_pawn_offset,
                            sizeof(state_pawn));
                if (state_pawn == pawn)
                {
                    const bool battle = in_battle(pawn);
                    if (battle != roxas_last_battle)
                    {
                        // Native idle can clear battle mode directly, without
                        // SetBattleMode. Observe that one flag in its existing
                        // active update; refresh once per transition only.
                        roxas_last_battle = battle;
                        if (character_swap::owns_swap_pawn(pawn))
                        {
                            try { refresh_idle(pawn); }
                            catch (...) {}
                        }
                    }
                }
            }
            if (kairi_relaxed || kairi_remaining_idle_seconds <= 0.0f)
            {
                return;
            }
            auto* pawn = kairi_owner.Get();
            UObject* state_pawn{};
            std::memcpy(&state_pawn, reinterpret_cast<std::byte*>(state) + state_owner_pawn_offset,
                        sizeof(state_pawn));
            if (!pawn || state_pawn != pawn || !kairi_instance.Get())
            {
                return;
            }
            if (in_battle(pawn))
            {
                kairi_remaining_idle_seconds = native_idle_delay_seconds;
                return;
            }
            // Kairi can report no equipped weapon, bypassing Sora's native
            // dismissal callback. Count only her existing active idle updates,
            // using Sora's verified three-second delay, and stop at completion.
            kairi_remaining_idle_seconds =
                std::max(0.0f, kairi_remaining_idle_seconds - std::max(0.0f, delta_seconds));
            if (kairi_remaining_idle_seconds == 0.0f)
            {
                set_weapon(pawn, false, true, 1.0f);
                try
                {
                    refresh_idle(pawn);
                }
                catch (...)
                {
                }
            }
        }

        void set_battle(UObject* pawn, bool enabled)
        {
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
            const character_swap_trace::NativeScope trace{"battle_mode"};
#endif
            const bool owned = pawn == kairi_owner.Get() && kairi_instance.Get();
            const bool roxas_owned = pawn == roxas_owner.Get() && roxas_instance.Get();
            const bool roxas_was_battle = roxas_owned && in_battle(pawn);
            const bool was_battle = owned && in_battle(pawn);
            reinterpret_cast<SetBattle>(original_battle)(pawn, enabled);
            if (owned)
            {
                kairi_jump_arms::visibility(pawn, !in_battle(pawn) && !kairi_weapon_visible(pawn));
            }
            if (roxas_owned) { roxas_last_battle = in_battle(pawn); }
            if (owned && enabled)
            {
                kairi_relaxed = false;
                kairi_remaining_idle_seconds = native_idle_delay_seconds;
            }
            if ((owned && was_battle != in_battle(pawn)) ||
                (roxas_owned && roxas_was_battle != in_battle(pawn)))
            {
                // A battle-mode transition can happen while already standing.
                // Refresh only that idle; never interrupt movement or an attack.
                try
                {
                    refresh_idle(pawn);
                }
                catch (...)
                {
                }
            }
        }

        bool install()
        {
            if (resolve_hook && battle_hook && play_hook && dismiss_hook && weapon_hook &&
                idle_update_hook)
            {
                return true;
            }
            const auto base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
            if (!character_native::verify_sites(
                    {character_native::Site::idle_resolve, character_native::Site::set_battle,
                     character_native::Site::idle_play, character_native::Site::dismiss_weapon,
                     character_native::Site::set_weapon, character_native::Site::idle_update}))
            {
                return false;
            }
            resolve_hook = std::make_unique<PLH::x64Detour>(
                base + resolve_rva, reinterpret_cast<uintptr_t>(&resolve), &original_resolve);
            battle_hook = std::make_unique<PLH::x64Detour>(
                base + battle_rva, reinterpret_cast<uintptr_t>(&set_battle), &original_battle);
            play_hook = std::make_unique<PLH::x64Detour>(
                base + play_rva, reinterpret_cast<uintptr_t>(&play), &original_play);
            dismiss_hook = std::make_unique<PLH::x64Detour>(
                base + dismiss_rva, reinterpret_cast<uintptr_t>(&dismiss_weapon),
                &original_dismiss);
            weapon_hook = std::make_unique<PLH::x64Detour>(
                base + weapon_rva, reinterpret_cast<uintptr_t>(&set_weapon), &original_weapon);
            idle_update_hook = std::make_unique<PLH::x64Detour>(
                base + idle_update_rva, reinterpret_cast<uintptr_t>(&idle_update),
                &original_idle_update);
            if (!resolve_hook->hook() || !original_resolve || !battle_hook->hook() ||
                !original_battle || !play_hook->hook() || !original_play || !dismiss_hook->hook() ||
                !original_dismiss || !weapon_hook->hook() || !original_weapon ||
                !idle_update_hook->hook() || !original_idle_update)
            {
                idle_update_hook.reset();
                weapon_hook.reset();
                dismiss_hook.reset();
                play_hook.reset();
                battle_hook.reset();
                resolve_hook.reset();
                return false;
            }
            return true;
        }
    } // namespace

    float movement_play_rate_scale(UObject* anim, FName name)
    {
        if (anim == kairi_instance.Get())
        {
            auto* pawn = kairi_owner.Get();
            if (pawn && character_swap::owns_swap_pawn(pawn) && !in_battle(pawn))
            {
                // Native Kairi running keeps its authored cadence. Walking keeps
                // the existing Aqua correction. Correct only loops, preserving
                // authored start/stop timing and native combat locomotion.
                const auto movement = relaxed_movement(name);
                if (movement != name)
                {
                    if (movement == kairi_movement[1].prepared) { return 1.0f; }
                    if (movement == kairi_movement[4].prepared) { return 1.15f; }
                }
            }
        }
        return 1.0f;
    }

    void clear_sneeze_finisher()
    {
        sneeze_finisher.reset();
        character_cinematic_proportions::publish_reaction({});
    }

    void prepare_sneeze_finisher(UObject* pawn, UObject* attack_state, bool airborne)
    {
        auto* anim = object(object(pawn, L"MyMesh"), L"AnimScriptInstance");
        auto* queue = object(pawn, L"MyStateComp");
        if (anim && queue)
        {
            if (sneeze_mesh.Get() != anim)
            {
                sneeze_mesh = make_uobject_weak(anim);
                sneeze_proportions.reset();
                try
                {
                    std::vector<UObject*> clips;
                    for (const auto* path : {
                        L"/Game/Character/pc/p_ex001/anm/rt/body/0239.0239",
                        L"/Game/Character/pc/p_ex001/anm/rt/body/0240.0240"})
                    {
                        if (auto* clip = find_loaded_runtime_asset(path)) { clips.push_back(clip); }
                    }
                    sneeze_proportions = character_cinematic_proportions::prepare(pawn, clips);
                }
                catch (const std::exception& failure)
                {
                    RC::Output::send<RC::LogLevel::Warning>(
                        STR("[KH3APProbeCpp] Sneeze proportion correction unavailable: {}\n"),
                        std::wstring(failure.what(), failure.what() + std::strlen(failure.what())));
                }
            }
            character_cinematic_proportions::publish_reaction(sneeze_proportions);
            sneeze_finisher = SneezeFinisher{make_uobject_weak(anim), make_uobject_weak(queue),
                make_uobject_weak(attack_state), FName(airborne ? L"BadStatAirSneeze" : L"BadStatSneeze", FNAME_Add),
                std::chrono::steady_clock::now() + std::chrono::milliseconds(300)};
        }
    }

    FName effective_animation_name(UObject* anim, FName name)
    {
        if (sneeze_finisher)
        {
            if (std::chrono::steady_clock::now() >= sneeze_finisher->expires)
            {
                sneeze_finisher.reset();
            }
            else if (sneeze_finisher->animation.Get() == anim &&
                     object(sneeze_finisher->queue.Get(), L"m_pCurrentState") == sneeze_finisher->state.Get())
            {
                // The alternate heroes request their numbered combo motions here.
                // Substitute the actual animation so its notifies and duration drive the finisher.
                const auto motion = name.ToString();
                if (motion.size() == 4 && motion.starts_with(L"13") &&
                    motion[2] >= L'0' && motion[2] <= L'9' && motion[3] >= L'0' && motion[3] <= L'9')
                {
                    return sneeze_finisher->motion;
                }
            }
        }
        if (anim == kairi_instance.Get())
        {
            auto* pawn = kairi_owner.Get();
            if (pawn && character_swap::owns_swap_pawn(pawn) && !in_battle(pawn))
            {
                const auto movement = relaxed_movement(name);
                if (movement != name) { return movement; }
                if (kairi_relaxed && name == kairi_battle_end)
                {
                    // Native Idle still requests BattleEnd after its three-second
                    // timer, even when relaxed locomotion already hid the weapon.
                    // Preserve the one-shot completion and native playback name
                    // while holding Kairi's relaxed pose instead of borrowing
                    // Sora's weapon-dismissal gesture. A drawn combat weapon keeps
                    // the normal BattleEnd because kairi_relaxed is then false.
                    return kairi_relaxed_idle;
                }
                if (kairi_relaxed && (name == kairi_idle_break || name == kairi_weapon_break))
                {
                    // Keep the native idle timer and playback name, but use
                    // Aqua's full-body neutral variation rather than Sora's
                    // fallback. The ordinary relaxed loop remains Idle_GM.
                    return kairi_aqua_idle_break;
                }
            }
        }
        if (anim == roxas_instance.Get() &&
            (name == roxas_battle_idle || name == roxas_relaxed_idle || name == roxas_weapon_break))
        {
            auto* pawn = roxas_owner.Get();
            // Only native Idle may select the relaxed pose. Attacks, shotlocks,
            // movement and scripted states retain their original animations.
            // Resolve current state only for an idle animation request, using
            // the prepared queue; there is no timer or idle-state discovery.
            if (pawn && character_swap::owns_swap_pawn(pawn) && roxas_is_idle())
            {
                if (in_battle(pawn)) { return roxas_battle_idle; }
                return name == roxas_weapon_break ? roxas_relaxed_break : roxas_relaxed_idle;
            }
        }
        if (name == kairi_battle_idle && anim == kairi_instance.Get())
        {
            auto* pawn = kairi_owner.Get();
            if (pawn && kairi_relaxed && !in_battle(pawn))
            {
                return kairi_relaxed_idle;
            }
        }
        return name;
    }

    bool initialize()
    {
        return !disabled && install();
    }

    bool prepare_roxas(UObject* pawn)
    {
        auto* id = is_uobject_valid_fast(pawn) ? pawn->GetPropertyByNameInChain(L"m_PlayerUniqueID") : nullptr;
        if (!id || id->GetElementSize() != 1 ||
            *id->ContainerPtrToValuePtr<uint8_t>(pawn) != player_id_value(PlayerId::Roxas)) { return true; }
        auto* anim = object(object(pawn, L"MyMesh"), L"AnimScriptInstance");
        if (roxas_owner.Get() == pawn && roxas_instance.Get() == anim) { return true; }
        auto* queue = object(pawn, L"MyStateComp");
        auto* state = object(queue, L"m_pCurrentState");
        auto* current = queue ? queue->GetPropertyByNameInChain(L"m_pCurrentState") : nullptr;
        auto* state_id = state ? state->GetPropertyByNameInChain(L"MyStateID") : nullptr;
        if (!character_swap::owns_swap_pawn(pawn) || !anim || !current || !state_id ||
            current->GetElementSize() != sizeof(UObject*) || state_id->GetElementSize() != 1 || !initialize())
        {
            return false;
        }
        roxas_battle_idle = FName(L"IdleB", FNAME_Add);
        roxas_relaxed_idle = FName(L"IdleN", FNAME_Add);
        roxas_weapon_break = FName(L"IdleBreakW", FNAME_Add);
        roxas_relaxed_break = FName(L"IdleBreakN", FNAME_Add);
        for (auto name : {roxas_battle_idle, roxas_relaxed_idle, roxas_relaxed_break})
        {
            if (!reinterpret_cast<Resolve>(original_resolve)(anim, name))
            {
                throw std::runtime_error("roxas_idle_animation_missing");
            }
        }
        prepare_roxas_run_pose();
        prepare_roxas_face(anim);
        roxas_current_state_offset = current->GetOffset_Internal();
        roxas_state_id_offset = state_id->GetOffset_Internal();
        roxas_owner = make_uobject_weak(pawn);
        roxas_instance = make_uobject_weak(anim);
        roxas_queue = make_uobject_weak(queue);
        roxas_last_battle = in_battle(pawn);
        if (roxas_is_idle() && !in_battle(pawn))
        {
            reinterpret_cast<SetWeapon>(original_weapon)(pawn, false, false, 1.0f);
        }
        refresh_idle(pawn);
        // The native pawn may have started NO_1_EL before our ownership was
        // bound. Restart that baseline once; later native requests resolve
        // through the private facial set, without a face-update timer.
        ReflectedFunctionCall face(anim, L"SQEX_FaceAnimPlay", "Roxas default face", 256);
        face.name(L"InAnimName", L"NO_1_EL");
        face.number(L"InBlendInTime", 0.1f);
        face.boolean(L"InLoop", true);
        face.number(L"InPlayRate", 1.0f);
        face.vector(L"InBlendParam", 0, 0, 0);
        face.boolean(L"InSameCheck", false);
        face.integer(L"InPlayPriority", 0);
        face.run();
        return true;
    }

    bool prepare_kairi(UObject* pawn)
    {
        if (disabled || !is_uobject_valid_fast(pawn))
        {
            return true;
        }
        auto* id = pawn->GetPropertyByNameInChain(L"m_PlayerUniqueID");
        if (!id || id->GetElementSize() != 1 ||
            *id->ContainerPtrToValuePtr<uint8_t>(pawn) != player_id_value(PlayerId::Kairi))
        {
            return true;
        }
        auto* anim = object(object(pawn, L"MyMesh"), L"AnimScriptInstance");
        if (!is_uobject_valid_fast(anim))
        {
            return false;
        }
        if (kairi_owner.Get() == pawn && kairi_instance.Get() == anim)
        {
            return true;
        }
        auto* set = find_loaded_runtime_asset(set_path);
        if (!is_uobject_valid_fast(set))
        {
            if (kairi_requested_owner.Get() != pawn)
            {
                if (!request_runtime_asset_load(pawn, set_path, nullptr,
                                                kairi_idle_animset_load_uuid))
                {
                    return true;
                }
                kairi_requested_owner = make_uobject_weak(pawn);
                kairi_load_deadline = std::chrono::steady_clock::now() + kairi_idle_animset_load_timeout;
            }
            if (std::chrono::steady_clock::now() >= kairi_load_deadline)
            {
                RC::Output::send<RC::LogLevel::Warning>(
                    STR("[KH3APProbeCpp] Kairi idle load timed out; preserving native idle\n"));
                return true;
            }
            return false;
        }
        auto* sets_property = anim->GetPropertyByNameInChain(L"MyAnimSets");
        auto* movement_source = find_loaded_runtime_asset(player_animation_support::aqua_animation_set_path);
        auto* run_source = find_loaded_runtime_asset(player_animation_support::riku_animation_set_path);
        if (!is_uobject_valid_fast(movement_source) || !is_uobject_valid_fast(run_source)) { return false; }
        if (anim->GetClassPrivate()->GetName() != L"p_ex006_Anim_C" ||
            set->GetClassPrivate()->GetName() != L"TresAnimSet" || !sets_property ||
            sets_property->GetClass().GetName() != L"ArrayProperty" ||
            sets_property->GetElementSize() != sizeof(TArray<UObject*>) || !initialize())
        {
            disabled = true;
            RC::Output::send<RC::LogLevel::Warning>(
                STR("[KH3APProbeCpp] Kairi idle injection unavailable; preserving native idle\n"));
            return true;
        }
        kairi_battle_idle = FName(L"IdleB", FNAME_Add);
        kairi_battle_end = FName(L"BattleEnd", FNAME_Add);
        kairi_relaxed_idle = FName(L"Idle_GM", FNAME_Add);
        auto* sets = sets_property->ContainerPtrToValuePtr<TArray<UObject*>>(anim);
        bool present = false;
        for (auto* existing : *sets)
        {
            present = present || existing == set;
        }
        if (!present)
        {
            sets->Add(set);
        }
        // MyAnimSets is a reflected strong reference. The installed animation
        // stays alive with this ABP; no global asset or NPC table is modified.
        if (!reinterpret_cast<Resolve>(original_resolve)(anim, kairi_relaxed_idle))
        {
            throw std::runtime_error("kairi_idle_animation_missing");
        }
        prepare_kairi_movement(anim, movement_source, *sets);
        prepare_kairi_pose_completion(set);
        auto* queue = object(pawn, L"MyStateComp");
        auto* state = object(queue, L"m_pCurrentState");
        auto* current = queue ? queue->GetPropertyByNameInChain(L"m_pCurrentState") : nullptr;
        auto* state_id = state ? state->GetPropertyByNameInChain(L"MyStateID") : nullptr;
        if (!current || !state_id || current->GetElementSize() != sizeof(UObject*) ||
            state_id->GetElementSize() != 1)
        {
            throw std::runtime_error("kairi_movement_state_layout");
        }
        kairi_queue = make_uobject_weak(queue);
        kairi_current_state_offset = current->GetOffset_Internal();
        kairi_state_id_offset = state_id->GetOffset_Internal();
        if (!kairi_jump_arms::prepare(pawn, anim, movement_source, run_source)) { return false; }
        kairi_owner = make_uobject_weak(pawn);
        kairi_instance = make_uobject_weak(anim);
        // Preparation can finish after native idle entry has already hidden
        // the weapon, so seed the state from the native getter once.
        const auto vtable = *reinterpret_cast<uintptr_t**>(pawn);
        const auto weapon_visible = reinterpret_cast<bool (*)(UObject*)>(
            vtable[kairi_weapon_visible_vtable_offset / sizeof(uintptr_t)]);
        kairi_relaxed = !in_battle(pawn) && !weapon_visible(pawn);
        kairi_remaining_idle_seconds = kairi_relaxed ? 0.0f : native_idle_delay_seconds;
        refresh_idle(pawn);
        RC::Output::send<RC::LogLevel::Normal>(
            STR("[KH3APProbeCpp] Kairi exploration ready: Idle_GM, native run with Riku arms and Aqua walk/idle break\n"));
        return true;
    }
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
    bool diagnostic_battle(bool enabled)
    {
        auto* pawn = character_swap::current_hero();
        const bool prepared = (pawn == kairi_owner.Get() && kairi_instance.Get()) ||
            (pawn == roxas_owner.Get() && roxas_instance.Get());
        auto* controller = object(pawn, L"Controller");
        if (!pawn || !prepared || object(controller, L"Pawn") != pawn ||
            actor_owning_world(pawn) != kh02_current_world())
        {
            return false;
        }
        ReflectedFunctionCall change(pawn, L"SetBattleMode", "Character idle battle validation",
                                 max_animation_parameter_bytes);
        change.boolean(L"InEnable", enabled);
        change.run();
        return in_battle(pawn) == enabled;
    }
#endif
} // namespace kh3ap::character_idle
