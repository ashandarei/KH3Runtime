#include "kh3ap/features/player/characters/character_profile.h"
#include <polyhook2/Detour/x64Detour.hpp>
#include "player_animation_support.h"
#include "kh3ap/features/player/characters/character_chest_voice.h"
#include "kh3ap/features/player/movement/alternate_wall_run.h"
#include "kh3ap/features/player/combat/kairi_ground_slam.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include <Unreal/UObject.hpp>
#include <Unreal/Property/FArrayProperty.hpp>
#include <Unreal/TArray.hpp>
#define NOMINMAX
#include <Windows.h>
#include <cstring>
#include <stdexcept>
#include <memory>

namespace kh3ap::player_animation_support
{
    using namespace RC::Unreal;
    namespace
    {
        using VoiceNotify = void (*)(UObject*, UObject*, UObject*);
        using VoiceStateBegin = void (*)(UObject*, UObject*, UObject*, float);
        uint64_t original_voice_notify{};
        uint64_t original_voice_state_begin{};
        std::unique_ptr<PLH::x64Detour> voice_notify_hook;
        std::unique_ptr<PLH::x64Detour> voice_state_begin_hook;

        void begin_voice_state(UObject* notify, UObject* mesh, UObject* animation, float duration)
        {
            if (auto* replacement = alternate_wall_run::replacement_voice(notify, mesh, animation))
            {
                auto* delay = notify->GetPropertyByNameInChain(L"m_StartWaitTime");
                auto* fade = notify->GetPropertyByNameInChain(L"FadeOutTime");
                if (delay && fade && delay->GetClass().GetName() == L"FloatProperty" &&
                    fade->GetClass().GetName() == L"FloatProperty" &&
                    *delay->ContainerPtrToValuePtr<float>(notify) == 0.0f &&
                    *fade->ContainerPtrToValuePtr<float>(notify) == 0.0f)
                {
                    // SuperSlideLoop starts its grunt through NotifyState, not
                    // Notify. Play the native donor once without registering
                    // Sora's pending voice: the original Tick/End then have no
                    // per-owner record to play or stop. Shared assets stay intact.
                    reinterpret_cast<VoiceNotify>(original_voice_notify)(replacement, mesh,
                                                                         animation);
                    return;
                }
            }
            reinterpret_cast<VoiceStateBegin>(original_voice_state_begin)(notify, mesh, animation,
                                                                          duration);
        }

        void play_voice(UObject* notify, UObject* mesh, UObject* animation)
        {
            if (auto* replacement =
                    character_chest_voice::replacement_voice(notify, mesh, animation))
            {
                reinterpret_cast<VoiceNotify>(original_voice_notify)(replacement, mesh, animation);
                return;
            }
            if (auto* replacement = alternate_wall_run::replacement_voice(notify, mesh, animation))
            {
                // Reuse the character's complete native voice selection (including
                // weighted choices or Roxas's voice group), at the original cue time.
                reinterpret_cast<VoiceNotify>(original_voice_notify)(replacement, mesh, animation);
                return;
            }
            if (!kairi_ground_slam::suppress_voice(notify, mesh, animation))
            {
                reinterpret_cast<VoiceNotify>(original_voice_notify)(notify, mesh, animation);
            }
        }

        const uintptr_t rebuild_skeleton_remaps_rva =
            character_native::rva(character_native::Site::skeleton_rebuild);
        const uintptr_t skeleton_compatible_rva =
            character_native::rva(character_native::Site::skeleton_compatible);

        // SQEX_CompatibleSkeletonLists element: skeleton pointer and two TArray<FName>s.
        struct CompatibleSkeleton
        {
            UObject* skeleton;
            TArray<FName> ignored_bones;
            TArray<FName> appended_bones;
        };
        static_assert(sizeof(CompatibleSkeleton) == 40);

        UObject* object_property(UObject* owner, const wchar_t* field)
        {
            return is_uobject_valid_fast(owner)
                       ? try_read_reflected_object_property_value(owner, field)
                       : nullptr;
        }
    } // namespace

    bool supported_skeleton_layout()
    {
        return character_native::verify_sites({character_native::Site::skeleton_rebuild,
                                               character_native::Site::skeleton_compatible});
    }

    bool initialize_voice_notify()
    {
        if (voice_notify_hook)
        {
            return voice_state_begin_hook != nullptr;
        }
        if (!character_native::verify_sites(
                {character_native::Site::voice_notify, character_native::Site::voice_state_begin}))
        {
            return false;
        }
        const auto address = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr)) +
                             character_native::rva(character_native::Site::voice_notify);
        auto hook = std::make_unique<PLH::x64Detour>(
            address, reinterpret_cast<uintptr_t>(&play_voice), &original_voice_notify);
        if (!hook->hook() || !original_voice_notify)
        {
            return false;
        }
        const auto state_address = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr)) +
                                   character_native::rva(character_native::Site::voice_state_begin);
        auto state_hook = std::make_unique<PLH::x64Detour>(
            state_address, reinterpret_cast<uintptr_t>(&begin_voice_state),
            &original_voice_state_begin);
        if (!state_hook->hook() || !original_voice_state_begin)
        {
            // Preserve the valid first hook if rollback cannot detach it.
            if (!hook->unHook())
            {
                voice_notify_hook = std::move(hook);
            }
            return false;
        }
        voice_notify_hook = std::move(hook);
        voice_state_begin_hook = std::move(state_hook);
        return true;
    }

    void allow_animation_skeleton(UObject* animation_instance, UObject* animation)
    {
        auto* target_skeleton = object_property(animation_instance, L"CurrentSkeleton");
        auto* source_skeleton = object_property(animation, L"Skeleton");
        auto* compatible_field =
            is_uobject_valid_fast(target_skeleton)
                ? target_skeleton->GetPropertyByNameInChain(L"SQEX_CompatibleSkeletonLists")
                : nullptr;
        if (!source_skeleton || !compatible_field || !compatible_field->IsA<FArrayProperty>() ||
            static_cast<FArrayProperty*>(compatible_field)->GetInner()->GetElementSize() !=
                sizeof(CompatibleSkeleton))
        {
            throw std::runtime_error("player_animation_skeleton_layout");
        }
        auto* compatible_skeletons =
            compatible_field->ContainerPtrToValuePtr<TArray<CompatibleSkeleton>>(target_skeleton);
        bool already_compatible = target_skeleton == source_skeleton;
        for (const auto& entry : *compatible_skeletons)
        {
            already_compatible = already_compatible || entry.skeleton == source_skeleton;
        }
        if (!already_compatible)
        {
            compatible_skeletons->Add({source_skeleton, {}, {}});
        }

        const auto module_base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
        const auto is_compatible =
            reinterpret_cast<bool (*)(UObject*, UObject*)>(module_base + skeleton_compatible_rva);
        if (!is_compatible(target_skeleton, source_skeleton))
        {
            // Rebuild the game's bone-name mappings; never reuse Sora's raw bone indices.
            const auto rebuild_remaps =
                reinterpret_cast<void (*)(UObject*)>(module_base + rebuild_skeleton_remaps_rva);
            rebuild_remaps(target_skeleton);
        }
        if (!is_compatible(target_skeleton, source_skeleton))
        {
            throw std::runtime_error("player_animation_skeleton_remap_failed");
        }
    }
} // namespace kh3ap::player_animation_support
