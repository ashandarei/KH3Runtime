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

namespace kh3ap::kairi_shining_hud
{
    // One hidden native movie, prepared on selection/map arrival. Native HUD
    // events still own its visibility, gauge updates and reset on departure.
    void select(int player_id);
    bool preparation_pending();
    void notify_player_started();
    void notify_map_loading();
    void before_swap(RC::Unreal::UObject* outgoing_pawn, int target_id);
    void tick(std::chrono::steady_clock::time_point now);
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
    nlohmann::json snapshot();
#endif
}
