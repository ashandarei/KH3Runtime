#pragma once
#include <Unreal/FWeakObjectPtr.hpp>

namespace kh3ap
{
    // Game-thread only. UE4SS copies an existing serial but does not allocate
    // one. Use the verified native assignment for objects whose serial is zero.
    // Failure returns an empty reference; never retain the raw pointer instead.
    [[nodiscard]] auto make_uobject_weak(RC::Unreal::UObject* object) -> RC::Unreal::FWeakObjectPtr;
}
