#pragma once

namespace RC::Unreal { class UObject; }

namespace kh3ap
{
    // Game-thread, actor-only, call-local lookup. Streamed actors can retain
    // their source package world as an Outer; Level.OwningWorld identifies the host.
    // The result is borrowed; retain world identity across frames as FWeakObjectPtr.
    auto actor_owning_world(RC::Unreal::UObject* actor) -> RC::Unreal::UObject*;
}
