#pragma once
#include <Unreal/FWeakObjectPtr.hpp>

namespace kh3ap
{
    // Shared source-world binding used by Castle and mirror sessions. Never owns raw actors across frames.
    struct Kh02GameplayBinding
    {
        RC::Unreal::FWeakObjectPtr world, pawn, script;
        bool started{};
    };
} // namespace kh3ap
