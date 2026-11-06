#include "kh02_matinee_binding.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/core/objects/uobject_weak.h"
#include "kh3ap/bridge/utilities.h"
#include <Unreal/UObject.hpp>
#include <Unreal/UClass.hpp>
#include <Unreal/FProperty.hpp>
#include <Unreal/NameTypes.hpp>
#include <cstring>
#include <stdexcept>
#include <utility>
#include "kh3ap/features/kh02/runtime/kh02_world_context.h"
#include <Unreal/UScriptStruct.hpp>
#include <Unreal/TArray.hpp>
#include <Unreal/Property/FArrayProperty.hpp>
#include <Unreal/Property/FStructProperty.hpp>

namespace kh3ap
{
    using namespace RC::Unreal;

    namespace
    {
        constexpr int actor_tag_limit = 128;
        constexpr int interpolation_group_limit = 256;

    } // namespace

    auto kh02_runtime_matinee_group_actor(UObject* matinee_actor, const wchar_t* group_name) -> UObject*
    {
        auto* property = matinee_actor->GetPropertyByNameInChain(L"GroupInst");
        if (!property || !property->IsA<FArrayProperty>())
        {
            return nullptr;
        }
        auto* groups = property->ContainerPtrToValuePtr<TArray<UObject*>>(matinee_actor);
        if (groups->Num() < 0 || groups->Num() > interpolation_group_limit)
        {
            return nullptr;
        }
        for (auto* instance : *groups)
        {
            auto* group = try_read_reflected_object_property_value(instance, L"Group");
            if (is_uobject_valid_fast(group) && group->GetName() == group_name)
            {
                return try_read_reflected_object_property_value(instance, L"GroupActor");
            }
        }
        return nullptr;
    }

    auto kh02_matinee_actor_slot(UObject* matinee, const wchar_t* group) -> UObject**
    {
        auto* p = matinee->GetPropertyByNameInChain(L"GroupActorInfos");
        if (!p || !p->IsA<FArrayProperty>())
        {
            throw std::runtime_error("kh02_story_groups");
        }
        auto* inner = static_cast<FArrayProperty*>(p)->GetInner();
        if (!inner || !inner->IsA<FStructProperty>())
        {
            throw std::runtime_error("kh02_story_group_struct");
        }
        auto* schema = static_cast<FStructProperty*>(inner)->GetStruct();
        auto* name = find_struct_field_by_name(schema, L"ObjectName");
        auto* actors = find_struct_field_by_name(schema, L"Actors");
        if (!name || name->GetClass().GetName() != L"NameProperty" || name->GetElementSize() != sizeof(FName) ||
            !actors || !actors->IsA<FArrayProperty>())
        {
            throw std::runtime_error("kh02_story_group_fields");
        }
        auto* actor_type = static_cast<FArrayProperty*>(actors)->GetInner();
        if (!actor_type || actor_type->GetClass().GetName() != L"ObjectProperty" ||
            actor_type->GetElementSize() != sizeof(UObject*))
        {
            throw std::runtime_error("kh02_story_group_actor_type");
        }

        struct Array
        {
            void* data;
            int32_t count, capacity;
        };

        auto* list = p->ContainerPtrToValuePtr<Array>(matinee);
        if (list->count < 1 || list->count > interpolation_group_limit || list->capacity < list->count || !list->data)
        {
            throw std::runtime_error("kh02_story_group_bounds");
        }
        for (int i = 0; i < list->count; ++i)
        {
            auto* item = static_cast<std::byte*>(list->data) + i * inner->GetElementSize();
            if (name->ContainerPtrToValuePtr<FName>(item)->ToString() != group)
            {
                continue;
            }
            auto* values = actors->ContainerPtrToValuePtr<Array>(item);
            if (values->count != 1 || values->capacity < 1 || !values->data)
            {
                throw std::runtime_error("kh02_story_aqua_slot");
            }
            return static_cast<UObject**>(values->data);
        }
        throw std::runtime_error("kh02_story_aqua_group_missing");
    }

    void kh02_bind_event_aqua(UObject* matinee_actor, UObject* event_pawn, const wchar_t* group_name)
    {
        if (!is_uobject_valid_fast(event_pawn) || kh02_actor_world(event_pawn) != kh02_actor_world(matinee_actor))
        {
            throw std::runtime_error("kh02_event_binding_world");
        }
        auto* tags_property = event_pawn->GetPropertyByNameInChain(L"Tags");
        auto* data = try_read_reflected_object_property_value(matinee_actor, L"MatineeData");
        auto* groups_property = is_uobject_valid_fast(data) ? data->GetPropertyByNameInChain(L"InterpGroups") : nullptr;
        if (!tags_property || !tags_property->IsA<FArrayProperty>() || !groups_property ||
            !groups_property->IsA<FArrayProperty>())
        {
            throw std::runtime_error("kh02_event_binding_schema");
        }
        const FName event_tag{L"KH02AP_EventAqua", FNAME_Add};
        auto* tags = tags_property->ContainerPtrToValuePtr<TArray<FName>>(event_pawn);
        if (tags->Num() < 0 || tags->Num() > actor_tag_limit)
        {
            throw std::runtime_error("kh02_event_binding_tags");
        }
        tags->AddUnique(event_tag);
        auto* groups = groups_property->ContainerPtrToValuePtr<TArray<UObject*>>(data);
        if (groups->Num() < 1 || groups->Num() > interpolation_group_limit)
        {
            throw std::runtime_error("kh02_event_binding_groups");
        }
        bool matched = false;
        for (auto* group : *groups)
        {
            if (!is_uobject_valid_fast(group) || group->GetName() != group_name)
            {
                continue;
            }
            auto* target = group->GetPropertyByNameInChain(L"m_TargetActorTag");
            if (!target || target->GetClass().GetName() != L"NameProperty" || target->GetElementSize() != sizeof(FName))
            {
                throw std::runtime_error("kh02_event_binding_target_tag");
            }
            // TresInterpGroup's character-name resolver otherwise replaces
            // GroupActorInfos with the possessed KH3 Aqua during InitGroupInst.
            // An explicit actor tag keeps the original event animation target.
            *target->ContainerPtrToValuePtr<FName>(group) = event_tag;
            matched = true;
        }
        if (!matched)
        {
            throw std::runtime_error("kh02_event_binding_group_missing");
        }
        *kh02_matinee_actor_slot(matinee_actor, group_name) = event_pawn;
    }

} // namespace kh3ap
