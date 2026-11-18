#pragma once

namespace RC::Unreal
{
    class UObject;
}

namespace kh3ap::fast_deathlink
{
    auto supported() -> bool;
    auto arm(RC::Unreal::UObject* pawn) -> bool;
    void confirm();
    void shutdown();
}
