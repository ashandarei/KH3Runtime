#pragma once

namespace RC::Unreal
{
    class UObject;
}

namespace kh3ap
{
    auto kh02_enemy_has_weakness(RC::Unreal::UObject* enemy, int element) -> bool;
}
