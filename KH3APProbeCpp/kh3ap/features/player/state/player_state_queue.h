#pragma once

#include "kh3ap/core/objects/uobject_validity.h"
#include <Unreal/Property/FArrayProperty.hpp>
#include <Unreal/TArray.hpp>
#include <Unreal/UObject.hpp>

namespace kh3ap
{
    // Borrow only on the game thread and before any call that can advance the state queue.
    inline auto read_player_state_queue(RC::Unreal::UObject* component)
        -> const RC::Unreal::TArray<RC::Unreal::UObject*>*
    {
        using namespace RC::Unreal;
        using StateQueue = TArray<UObject*>;
        constexpr int max_state_queue_entries = 64;
        auto* property = is_uobject_valid_fast(component) ? component->GetPropertyByNameInChain(L"m_StateQueue")
                                                          : nullptr;
        if (!property || !property->IsA<FArrayProperty>() || property->GetElementSize() != sizeof(StateQueue))
        {
            return nullptr;
        }
        auto* inner = const_cast<FProperty*>(static_cast<FArrayProperty*>(property)->GetInner());
        if (!inner || inner->GetClass().GetName() != L"ObjectProperty" || inner->GetElementSize() != sizeof(UObject*))
        {
            return nullptr;
        }
        const auto* queue = property->ContainerPtrToValuePtr<StateQueue>(component);
        if (queue->Num() < 0 || queue->Num() > max_state_queue_entries ||
            queue->Max() < queue->Num() || (queue->Num() && !queue->GetData()))
        {
            return nullptr;
        }
        return queue;
    }
}
