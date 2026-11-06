#pragma once
#include "kh02_lifecycle.h"

namespace RC::Unreal
{
    class UObject;
}

namespace kh3ap
{
    // Game-thread, borrowed results. Read viewport/world live; cache identity only.
    // Actor-only, call-local lookup. Imported streamed actors retain their
    // source package world as an Outer; Level.OwningWorld identifies the host.
    auto kh02_actor_world(RC::Unreal::UObject* actor) -> RC::Unreal::UObject*;
    // Presentation hero only; this does not authorize AP grant contexts.
    auto kh02_story_player(RC::Unreal::UObject* fallback) -> RC::Unreal::UObject*;
    auto kh02_current_story_world() -> RC::Unreal::UObject*;
    auto kh02_current_viewport() -> RC::Unreal::UObject*;
    auto kh02_current_world() -> RC::Unreal::UObject*;
    auto kh02_world_kind(RC::Unreal::UObject* world) -> Kh02WorldKind;
} // namespace kh3ap
