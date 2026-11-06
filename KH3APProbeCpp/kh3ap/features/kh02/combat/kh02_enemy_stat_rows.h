#pragma once
#include <Unreal/FWeakObjectPtr.hpp>
#include <vector>

namespace kh3ap
{
    struct Kh02EnemyCombatValue
    {
        const wchar_t* field;
        int original;
        int adapted;
    };

    struct Kh02EnemyStatRow
    {
        RC::Unreal::FWeakObjectPtr table;
        uint64_t name;
        int original;
        float rate;
        int reference_hp{};
        std::vector<Kh02EnemyCombatValue> combat;
    };

} // namespace kh3ap
