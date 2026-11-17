#pragma once
#include "kh3ap/core/performance_build.h"
#include <Unreal/NameTypes.hpp>
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
#include <nlohmann/json.hpp>
#endif
namespace RC::Unreal { class UObject; }

namespace kh3ap::kairi_jump_arms
{
    bool prepare(RC::Unreal::UObject* pawn, RC::Unreal::UObject* anim, RC::Unreal::UObject* donor_set, RC::Unreal::UObject* run_donor_set);
    void playback(RC::Unreal::UObject* pawn, RC::Unreal::FName name, RC::Unreal::FName slot, bool unarmed);
    void visibility(RC::Unreal::UObject* pawn, bool unarmed);
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
    nlohmann::json snapshot();
#endif
}
