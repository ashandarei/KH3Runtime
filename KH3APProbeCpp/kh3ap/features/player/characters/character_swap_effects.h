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

namespace kh3ap::character_swap_effects
{
    // Optional presentation: none of these operations gate character control.
    void select(bool enabled);
    void selected_class_ready(RC::Unreal::UObject* character_class);
    void notify_map_loading();
    void notify_player_started();
    void tick(std::chrono::steady_clock::time_point now);
    void departing(RC::Unreal::UObject* pawn);
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
    void suppress_for_test(std::chrono::milliseconds duration);
    void capture_next_for_test();
    bool preview(RC::Unreal::UObject* pawn);
    nlohmann::json snapshot();
#endif
} // namespace kh3ap::character_swap_effects
