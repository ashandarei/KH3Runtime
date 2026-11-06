#pragma once

namespace RC::Unreal
{
    class UObject;
}

namespace kh3ap
{
    // The returned slot is borrowed for one synchronous call; never retain it.
    auto kh02_matinee_actor_slot(RC::Unreal::UObject* matinee, const wchar_t* group = L"TresInterpGroup_21")
        -> RC::Unreal::UObject**;
    auto kh02_runtime_matinee_group_actor(RC::Unreal::UObject* matinee, const wchar_t* group) -> RC::Unreal::UObject*;
    void kh02_bind_event_aqua(RC::Unreal::UObject* matinee, RC::Unreal::UObject* event_pawn, const wchar_t* group);
} // namespace kh3ap
