#pragma once

#include <cstddef>
#include <vector>

namespace RC::Unreal
{
    class AActor;
    class UClass;
    class UObject;
}

namespace kh3ap
{
    // Game-thread only. Resolve reflection on the current owner for each scan;
    // retain returned actors across frames only as FWeakObjectPtr.
    [[nodiscard]] auto find_live_actors_of_class(
            RC::Unreal::UObject* world_context,
            RC::Unreal::UClass* actor_class,
            size_t limit) -> std::vector<RC::Unreal::AActor*>;
}
