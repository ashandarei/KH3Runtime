#pragma once

#include <Unreal/FWeakObjectPtr.hpp>

namespace kh3ap
{
    // Preserve UObject serial identity instead of accepting a new object that
    // happens to reuse the same address after garbage collection.
    [[nodiscard]] inline auto weak_uobject_resolver(RC::Unreal::UObject* object)
    {
        RC::Unreal::FWeakObjectPtr weak{};
        weak = object;
        return [weak]() { return weak.Get(); };
    }
}
