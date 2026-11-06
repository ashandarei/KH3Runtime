#include "kh3ap/features/player/characters/character_profile.h"
#include "player_animation_support.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include <Unreal/UObject.hpp>
#include <Unreal/Property/FArrayProperty.hpp>
#include <Unreal/TArray.hpp>
#define NOMINMAX
#include <Windows.h>
#include <cstring>
#include <stdexcept>

namespace kh3ap::player_animation_support
{
    using namespace RC::Unreal;
    namespace
    {
        const uintptr_t rebuild_skeleton_remaps_rva = character_native::rva(character_native::Site::skeleton_rebuild);
        const uintptr_t skeleton_compatible_rva = character_native::rva(character_native::Site::skeleton_compatible);

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
        return character_native::verify_sites({character_native::Site::skeleton_rebuild, character_native::Site::skeleton_compatible});
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
