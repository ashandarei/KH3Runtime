#pragma once
#include "kh3ap/core/performance_build.h"
#include <chrono>
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
#include <nlohmann/json_fwd.hpp>
#endif

namespace RC::Unreal
{
    class UObject;
}

namespace kh3ap::character_swap_assets
{
    // Game-thread selection and player-start events arm one bounded preload.
    void select(int player_id);
    bool preparation_pending(int player_id);
    void notify_player_started();
    void notify_player_swap_started(RC::Unreal::UObject* outgoing_pawn);
    void tick(std::chrono::steady_clock::time_point now);
    void retain_return_class(RC::Unreal::UObject* sora);
    void retain_return_dependency(RC::Unreal::UObject* asset);
    void release_return_class();
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
    nlohmann::json snapshot();
#endif
} // namespace kh3ap::character_swap_assets
