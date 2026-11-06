#pragma once

namespace RC::Unreal
{
    class UObject;
}

namespace kh3ap
{
    // Readiness is independent of item grant authorization and never fills its cache.
    bool runtime_sora_control_ready(RC::Unreal::UObject* pawn);
    bool runtime_sora_game_over(RC::Unreal::UObject* pawn);
} // namespace kh3ap
