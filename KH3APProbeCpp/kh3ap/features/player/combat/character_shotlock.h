#pragma once
#include "kh3ap/core/performance_build.h"
#include "kh3ap/features/player/player_state_ids.h"
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
#include <nlohmann/json_fwd.hpp>
#include <chrono>
#endif

namespace RC::Unreal
{
    class UObject;
}

namespace kh3ap::character_shotlock
{
    inline constexpr auto oblivion_max_animation_set_path =
        L"/Game/Character/pc/p_ex001/anm/p_ex001_AnimSet23.p_ex001_AnimSet23";
    inline constexpr auto oblivion_projectile_class_path =
        L"/Game/Blueprints/Projectile/ShootFlow/TresProjDF_Burst.TresProjDF_Burst_C";
    inline constexpr auto oblivion_max_projectile_class_path =
        L"/Game/Blueprints/Projectile/ShootFlow/TresProjTF_LightInception.TresProjTF_LightInception_C";
    inline constexpr int state_id = player_state_ids::shoot_flow;
    bool initialize();
    bool prepare(RC::Unreal::UObject* pawn);
    // Aim begins before its visible phase; its camera and scope exit later.
    // Query those states only for a swap, independently of input bindings.
    bool blocks_swap(RC::Unreal::UObject* pawn);
    enum class SwapInputStatus { Waiting, Clear, Blocked };
    // A queued swap must cross a native aim-input pass before committing. This
    // observes the logical request, before any vanilla hold timer advances.
    bool begin_swap_input_check(RC::Unreal::UObject* pawn);
    SwapInputStatus swap_input_status(RC::Unreal::UObject* pawn);
    void finish_swap_input_check();
    void prepare_incoming_input(RC::Unreal::UObject* pawn);
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
    nlohmann::json snapshot(RC::Unreal::UObject* pawn);
    nlohmann::json diagnostic_scope_mode(RC::Unreal::UObject* pawn, int mode);
    bool diagnostic_race_start(RC::Unreal::UObject* pawn, int order);
    void diagnostic_race_tick(std::chrono::steady_clock::time_point now);
    void diagnostic_after_factory(RC::Unreal::UObject* pawn);
#endif
} // namespace kh3ap::character_shotlock
