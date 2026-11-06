#include "kh3ap/features/player/characters/character_cinematic_binding.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/core/objects/uobject_weak.h"
#include "kh3ap/features/kh02/cutscenes/kh02_matinee_binding.h"
#include "kh3ap/features/kh02/runtime/kh02_world_context.h"
#include <Unreal/UClass.hpp>
#include <Unreal/UObject.hpp>
#include <Unreal/FProperty.hpp>
#include <Unreal/NameTypes.hpp>
#include <Unreal/Property/FArrayProperty.hpp>
#include <Unreal/TArray.hpp>
#include <DynamicOutput/DynamicOutput.hpp>
#include <algorithm>
#include <array>
#include <vector>

namespace kh3ap::character_cinematic_binding
{
    using namespace RC::Unreal;
    namespace
    {
        constexpr int actor_tag_count_limit = 128;
        constexpr int maximum_cinematic_groups = 512;

        struct Binding
        {
            FWeakObjectPtr matinee, group, pawn, original_actor, world;
            FName original_tag;
        };
        std::vector<Binding> bindings;
        constexpr auto hero_tag = L"KH3AP_CinematicAlternate";

        auto actor_tags(UObject* pawn) -> TArray<FName>*
        {
            auto* field =
                is_uobject_valid_fast(pawn) ? pawn->GetPropertyByNameInChain(L"Tags") : nullptr;
            if (!field || !field->IsA<FArrayProperty>())
            {
                return nullptr;
            }
            auto* tags = field->ContainerPtrToValuePtr<TArray<FName>>(pawn);
            return tags->Num() >= 0 && tags->Num() < actor_tag_count_limit ? tags : nullptr;
        }

        auto sora_group(UObject* matinee) -> UObject*
        {
            if (!is_uobject_valid_fast(matinee) ||
                kh02_actor_world(matinee) != kh02_current_world())
            {
                return nullptr;
            }
            auto* data = try_read_reflected_object_property_value(matinee, L"MatineeData");
            auto* field = is_uobject_valid_fast(data)
                              ? data->GetPropertyByNameInChain(L"InterpGroups")
                              : nullptr;
            if (!field || !field->IsA<FArrayProperty>())
            {
                return nullptr;
            }
            auto* groups = field->ContainerPtrToValuePtr<TArray<UObject*>>(data);
            if (groups->Num() < 1 || groups->Num() > maximum_cinematic_groups)
            {
                return nullptr;
            }
            constexpr std::array<std::wstring_view, 9> meshes{L"p_ex001", L"p_ca001", L"p_mi001",
                                                              L"p_ts001", L"p_po001", L"p_bx001",
                                                              L"p_ex011", L"p_ew001", L"p_dl001"};
            UObject* selected = nullptr;
            for (auto* group : *groups)
            {
                if (!is_uobject_valid_fast(group))
                {
                    continue;
                }
                const auto mesh = try_read_object_string_property(group, L"m_CharacterMesh");
                if (!mesh || std::find(meshes.begin(), meshes.end(), *mesh) == meshes.end())
                {
                    continue;
                }
                // Multiple Sora roles can represent distinct story actors.
                if (selected)
                {
                    return nullptr;
                }
                selected = group;
            }
            return selected;
        }

        auto tag_slot(UObject* group) -> FName*
        {
            auto* field = is_uobject_valid_fast(group)
                              ? group->GetPropertyByNameInChain(L"m_TargetActorTag")
                              : nullptr;
            if (!field || field->GetClass().GetName() != L"NameProperty" ||
                field->GetElementSize() != sizeof(FName))
            {
                return nullptr;
            }
            return field->ContainerPtrToValuePtr<FName>(group);
        }
    } // namespace

    bool can_bind(UObject* matinee)
    {
        auto* group = sora_group(matinee);
        auto* tag = tag_slot(group);
        if (!tag || (tag->ToString() != L"None" && tag->ToString() != hero_tag))
        {
            return false;
        }
        try
        {
            return kh02_matinee_actor_slot(matinee, group->GetName().c_str()) != nullptr;
        }
        catch (...)
        {
            return false;
        }
    }

    bool bind(UObject* matinee, UObject* pawn)
    {
        if (!can_bind(matinee) || !is_uobject_valid_fast(pawn) ||
            kh02_actor_world(pawn) != kh02_actor_world(matinee))
        {
            return false;
        }
        auto* tags = actor_tags(pawn);
        if (!tags)
        {
            return false;
        }
        std::erase_if(bindings,
                      [](const Binding& value)
                      {
                          if (value.world.Get() && value.matinee.Get())
                          {
                              return false;
                          }
                          if (auto* old_tags = actor_tags(value.pawn.Get()))
                          {
                              old_tags->Remove(FName(hero_tag, FNAME_Add));
                          }
                          return true;
                      });
        auto* group = sora_group(matinee);
        auto* tag = tag_slot(group);
        auto* slot = kh02_matinee_actor_slot(matinee, group->GetName().c_str());
        const auto found = std::find_if(bindings.begin(), bindings.end(),
                                        [matinee](const Binding& value)
                                        {
                                            return value.matinee.Get() == matinee;
                                        });
        if (found == bindings.end())
        {
            bindings.push_back({make_uobject_weak(matinee), make_uobject_weak(group),
                                make_uobject_weak(pawn), make_uobject_weak(*slot),
                                make_uobject_weak(kh02_actor_world(pawn)), *tag});
        }
        else if (found->pawn.Get() != pawn)
        {
            if (auto* old_tags = actor_tags(found->pawn.Get()))
            {
                old_tags->Remove(FName(hero_tag, FNAME_Add));
            }
            found->pawn = make_uobject_weak(pawn);
        }
        const FName target(hero_tag, FNAME_Add);
        tags->AddUnique(target);
        *tag = target;
        *slot = pawn;
        RC::Output::send<RC::LogLevel::Normal>(
            STR("[KH3APProbeCpp] CharacterCinematic bound scene={} pawn={}\n"), matinee->GetName(),
            pawn->GetName());
        return true;
    }

    void release(UObject* matinee)
    {
        for (auto iterator = bindings.begin(); iterator != bindings.end();)
        {
            if (iterator->matinee.Get() != matinee)
            {
                ++iterator;
                continue;
            }
            auto* group = iterator->group.Get();
            auto* pawn = iterator->pawn.Get();
            if (iterator->world.Get() == kh02_current_world())
            {
                if (auto* tag = tag_slot(group); tag && tag->ToString() == hero_tag)
                {
                    *tag = iterator->original_tag;
                }
                if (auto* tags = actor_tags(pawn))
                {
                    tags->Remove(FName(hero_tag, FNAME_Add));
                }
                try
                {
                    if (is_uobject_valid_fast(group))
                    {
                        auto* slot = kh02_matinee_actor_slot(matinee, group->GetName().c_str());
                        if (*slot == pawn)
                        {
                            *slot = iterator->original_actor.Get();
                        }
                    }
                }
                catch (...)
                {
                }
            }
            iterator = bindings.erase(iterator);
        }
    }
} // namespace kh3ap::character_cinematic_binding
