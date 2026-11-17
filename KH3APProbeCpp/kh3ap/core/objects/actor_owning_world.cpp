#include "actor_owning_world.h"
#include "kh3ap/core/objects/uobject_validity.h"
#include "kh3ap/core/reflection/reflected_property_cache.h"
#include <Unreal/AActor.hpp>
#include <Unreal/World.hpp>

namespace kh3ap
{
    using namespace RC::Unreal;

    auto actor_owning_world(UObject* actor) -> UObject*
    {
        if (!is_uobject_valid_fast(actor))
        {
            return nullptr;
        }
        // Cache only immutable layouts; world membership is read live.
        thread_local ReflectedPropertyCache<32> properties;
        auto* level = actor->GetOuterPrivate();
        if (auto* owning = properties.read_object(level, L"OwningWorld");
            is_uobject_valid_fast(owning))
        {
            return owning;
        }
        return static_cast<AActor*>(actor)->GetWorld();
    }
}
