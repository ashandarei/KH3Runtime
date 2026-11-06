#pragma once

#include <Unreal/FWeakObjectPtr.hpp>

namespace RC::Unreal { class UObject; class UFunction; }

namespace kh3ap
{
    struct RuntimeBattleLevelFunction
    {
        RC::Unreal::UObject* library{};
        RC::Unreal::UFunction* function{};
    };

    // Game-thread-only. Retains serial-checked weak references and resolves
    // functions on their owning class instead of searching GUObjectArray.
    class RuntimeBattleLevelFunctions
    {
    public:
        auto resolve(bool getter) -> RuntimeBattleLevelFunction;
        auto prepare() -> void;
    private:
        RC::Unreal::FWeakObjectPtr library_{};
        RC::Unreal::FWeakObjectPtr get_{};
        RC::Unreal::FWeakObjectPtr set_{};
    };
}
