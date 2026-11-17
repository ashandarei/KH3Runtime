#pragma once
#include "kh02_lifecycle.h"
#include "kh3ap/core/objects/actor_owning_world.h"

namespace RC::Unreal
{
    class UObject;
}

namespace kh3ap
{
    // Game-thread, borrowed results. Read viewport/world live; cache identity only.
    // Presentation hero only; this does not authorize AP grant contexts.
    auto kh02_story_player(RC::Unreal::UObject* fallback) -> RC::Unreal::UObject*;
    auto kh02_current_story_world() -> RC::Unreal::UObject*;
    auto kh02_current_viewport() -> RC::Unreal::UObject*;
    auto kh02_current_world() -> RC::Unreal::UObject*;
    auto kh02_world_kind(RC::Unreal::UObject* world) -> Kh02WorldKind;
} // namespace kh3ap
