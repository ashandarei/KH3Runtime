#pragma once
#include "kh3ap/features/player/player_state_ids.h"
#include "kh3ap/core/performance_build.h"

namespace RC::Unreal
{
    class UObject;
}

namespace kh3ap::alternate_wall_run
{
    // ETresStateID::TSID_STATE_WALLRUN in the shipping state factory.
    inline constexpr int state_id = player_state_ids::wall_run;
    bool prepare(RC::Unreal::UObject* pawn);
    void capture_sora(RC::Unreal::UObject* pawn);
    bool ready(RC::Unreal::UObject* pawn);
    void refresh_unlocks(RC::Unreal::UObject* pawn, bool enabled);
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
    bool diagnostic_move(RC::Unreal::UObject* pawn, float x, float y);
    void diagnostic_tick();
#endif
} // namespace kh3ap::alternate_wall_run
