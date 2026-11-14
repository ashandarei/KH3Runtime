#include "kh3ap/features/player/characters/character_cinematic_binding.h"
#include "kh3ap/features/player/characters/character_cinematic_policy.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/core/objects/uobject_weak.h"
#include "kh3ap/features/kh02/cutscenes/kh02_matinee_binding.h"
#include "kh3ap/features/kh02/runtime/kh02_world_context.h"
#include <Unreal/UClass.hpp>
#include <Unreal/UObject.hpp>
#include <Unreal/UObjectGlobals.hpp>
#include <Unreal/FProperty.hpp>
#include <Unreal/NameTypes.hpp>
#include <Unreal/Property/FArrayProperty.hpp>
#include <Unreal/Property/FBoolProperty.hpp>
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

        auto sora_groups(UObject* matinee) -> std::vector<UObject*>
        {
            if (!is_uobject_valid_fast(matinee) ||
                kh02_actor_world(matinee) != kh02_current_world())
            {
                return {};
            }
            auto* data = try_read_reflected_object_property_value(matinee, L"MatineeData");
            auto* field = is_uobject_valid_fast(data)
                              ? data->GetPropertyByNameInChain(L"InterpGroups")
                              : nullptr;
            if (!field || !field->IsA<FArrayProperty>())
            {
                return {};
            }
            auto* groups = field->ContainerPtrToValuePtr<TArray<UObject*>>(data);
            if (groups->Num() < 1 || groups->Num() > maximum_cinematic_groups)
            {
                return {};
            }
            constexpr std::array<std::wstring_view, 9> meshes{L"p_ex001", L"p_ca001", L"p_mi001",
                                                              L"p_ts001", L"p_po001", L"p_bx001",
                                                              L"p_ex011", L"p_ew001", L"p_dl001"};
            auto* inner = static_cast<FArrayProperty*>(field)->GetInner();
            if (!inner || inner->GetClass().GetName() != L"ObjectProperty" ||
                inner->GetElementSize() != sizeof(UObject*))
            {
                return {};
            }
            std::vector<UObject*> selected;
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
                selected.push_back(group);
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

        bool presentation_only(UObject* group)
        {
            auto* field = group->GetPropertyByNameInChain(L"InterpTracks");
            if (!field || !field->IsA<FArrayProperty>())
            {
                return false;
            }
            auto* inner = static_cast<FArrayProperty*>(field)->GetInner();
            if (!inner || inner->GetClass().GetName() != L"ObjectProperty" ||
                inner->GetElementSize() != sizeof(UObject*))
            {
                return false;
            }
            const auto* tracks = field->ContainerPtrToValuePtr<TArray<UObject*>>(group);
            if (tracks->Num() < 1 || tracks->Num() > maximum_cinematic_groups)
            {
                return false;
            }
            for (auto* track : *tracks)
            {
                if (!is_uobject_valid_fast(track))
                {
                    return false;
                }
                auto* disabled = track->GetPropertyByNameInChain(L"bDisableTrack");
                if (!disabled || !disabled->IsA<FBoolProperty>())
                {
                    return false;
                }
                if (static_cast<FBoolProperty*>(disabled)->GetPropertyValueInContainer(track))
                {
                    continue;
                }
                if (!presentation_track(track->GetClassPrivate()->GetName()))
                {
                    return false;
                }
                auto* children = track->GetPropertyByNameInChain(L"SubTracks");
                if (!children || !children->IsA<FArrayProperty>() ||
                    children->ContainerPtrToValuePtr<TArray<UObject*>>(track)->Num() != 0)
                {
                    return false;
                }
            }
            return true;
        }

        struct Target
        {
            UObject* group;
            FName* tag;
            UObject** slot;
        };

        auto targets(UObject* matinee) -> std::vector<Target>
        {
            std::vector<Target> result;
            std::vector<PlayerRole> roles;
            const auto groups = sora_groups(matinee);
            for (auto* group : groups)
            {
                auto* tag = tag_slot(group);
                if (!tag || (tag->ToString() != L"None" && tag->ToString() != hero_tag))
                {
                    return {};
                }
                auto* slot = kh02_matinee_actor_slot(matinee, group->GetName().c_str());
                if (!slot)
                {
                    return {};
                }
                result.push_back({group, tag, slot});
                if (groups.size() > 1)
                {
                    auto* actor = *slot;
                    bool player_actor = false;
                    if (is_uobject_valid_fast(actor) &&
                        kh02_actor_world(actor) == kh02_actor_world(matinee))
                    {
                        auto* player_class = UObjectGlobals::StaticFindObject<UClass*>(
                            nullptr, nullptr, L"/Script/TresGame.TresPlayerPawnBase");
                        player_actor = is_uobject_a_fast(actor, player_class);
                    }
                    roles.push_back({presentation_only(group), player_actor,
                                     reinterpret_cast<uintptr_t>(actor)});
                }
            }
            if (groups.size() > 1 && !can_share_player_roles(matinee->GetPathName(), roles))
            {
                return {};
            }
            return result;
        }

        void remove_unused_tag(UObject* pawn)
        {
            if (std::any_of(bindings.begin(), bindings.end(),
                            [pawn](const Binding& value)
                            {
                                return value.pawn.Get() == pawn && value.matinee.Get() &&
                                       value.world.Get();
                            }))
            {
                return;
            }
            if (auto* tags = actor_tags(pawn))
            {
                tags->Remove(FName(hero_tag, FNAME_Add));
            }
        }
    } // namespace

    bool can_bind(UObject* matinee)
    {
        try
        {
            return !targets(matinee).empty();
        }
        catch (...)
        {
            return false;
        }
    }

    bool bind(UObject* matinee, UObject* pawn)
    {
        if (!is_uobject_valid_fast(pawn) || !is_uobject_valid_fast(matinee) ||
            kh02_actor_world(pawn) != kh02_actor_world(matinee))
        {
            return false;
        }
        auto* tags = actor_tags(pawn);
        if (!tags)
        {
            return false;
        }
        // Resolve every role before changing any of them. A missing slot must
        // leave the entire scene eligible for the existing native-Sora fallback.
        const auto plan = targets(matinee);
        if (plan.empty())
        {
            return false;
        }
        std::vector<FWeakObjectPtr> old_pawns;
        std::erase_if(bindings,
                      [&old_pawns](const Binding& value)
                      {
                          if (value.world.Get() && value.matinee.Get() && value.group.Get())
                          {
                              return false;
                          }
                          old_pawns.push_back(value.pawn);
                          return true;
                      });
        bindings.reserve(bindings.size() + plan.size());
        old_pawns.reserve(old_pawns.size() + plan.size());
        const FName target(hero_tag, FNAME_Add);
        tags->AddUnique(target);
        for (const auto& item : plan)
        {
            const auto found = std::find_if(bindings.begin(), bindings.end(),
                                            [matinee, &item](const Binding& value)
                                            {
                                                return value.matinee.Get() == matinee &&
                                                       value.group.Get() == item.group;
                                            });
            if (found == bindings.end())
            {
                bindings.push_back({make_uobject_weak(matinee), make_uobject_weak(item.group),
                                    make_uobject_weak(pawn), make_uobject_weak(*item.slot),
                                    make_uobject_weak(kh02_actor_world(pawn)), *item.tag});
            }
            else if (found->pawn.Get() != pawn)
            {
                old_pawns.push_back(found->pawn);
                found->pawn = make_uobject_weak(pawn);
            }
            *item.tag = target;
            *item.slot = pawn;
        }
        for (const auto& old : old_pawns)
        {
            remove_unused_tag(old.Get());
        }
        RC::Output::send<RC::LogLevel::Normal>(
            STR("[KH3APProbeCpp] CharacterCinematic bound scene={} pawn={} roles={}\n"),
            matinee->GetName(), pawn->GetName(), plan.size());
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
                try
                {
                    if (is_uobject_valid_fast(group))
                    {
                        auto* slot = kh02_matinee_actor_slot(matinee, group->GetName().c_str());
                        if (slot && *slot == pawn)
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
            remove_unused_tag(pawn);
        }
    }
} // namespace kh3ap::character_cinematic_binding
