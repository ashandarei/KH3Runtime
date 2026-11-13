#pragma once
#include <chrono>
#include <cstdint>
#include "kh3ap/core/performance_build.h"
#include <nlohmann/json_fwd.hpp>

namespace RC::Unreal
{
    class UObject;
}

namespace kh3ap::betrayal_trap
{
    bool start(RC::Unreal::UObject* controlled_player);
    void cancel();
    void prepare_player_swap(RC::Unreal::UObject* outgoing);
    void handoff_player(RC::Unreal::UObject* incoming);
    void notify_player_started();
    void observe_constructed_object(RC::Unreal::UObject* object);
    void observe_state(uintptr_t component, uintptr_t state);
    void tick(std::chrono::steady_clock::time_point now);
    bool active();
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
    nlohmann::json snapshot(RC::Unreal::UObject* controlled_player);
#endif
} // namespace kh3ap::betrayal_trap
