#pragma once

namespace RC::Unreal
{
    class UObject;
}

namespace kh3ap
{
    auto kh02_challenge_current_state(RC::Unreal::UObject* actor) -> RC::Unreal::UObject*;
    auto kh02_is_blizzard_rail(RC::Unreal::UObject* rail) -> bool;
} // namespace kh3ap
