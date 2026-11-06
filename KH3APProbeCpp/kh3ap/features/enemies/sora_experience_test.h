#pragma once
#include <cstdint>
namespace RC::Unreal { class UObject; }
namespace kh3ap
{
    struct SoraExperienceSnapshot { int32_t level{-1}; int32_t total_exp{-1}; };
    // Diagnostic-only; called synchronously on the game thread with current Sora.
    auto inspect_sora_experience(RC::Unreal::UObject* pawn) -> SoraExperienceSnapshot;
    auto award_sora_experience(RC::Unreal::UObject* pawn, int32_t amount) -> void;
    auto award_kh02_aqua_experience(RC::Unreal::UObject* pawn, int32_t amount) -> void;
}
