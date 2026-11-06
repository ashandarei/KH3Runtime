#pragma once

namespace RC::Unreal
{
    class UObject;
}

namespace kh3ap::character_cinematic_binding
{
    bool can_bind(RC::Unreal::UObject* matinee);
    bool bind(RC::Unreal::UObject* matinee, RC::Unreal::UObject* pawn);
    void release(RC::Unreal::UObject* matinee);
} // namespace kh3ap::character_cinematic_binding
