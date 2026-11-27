#pragma once

namespace RC::Unreal
{
    class UObject;
    struct FName;
}

namespace kh3ap::character_idle
{
    // Selection-time setup; leaves live pawns and animation ownership alone.
    bool initialize();
    // Called only while a character swap/rebind is unfinished. False means
    // the installed animation set is still loading; no idle polling is needed.
    bool prepare_kairi(RC::Unreal::UObject* pawn);
    // Reuses the shared animation/battle hooks after traversal supplies IdleN.
    // Roxas selects his idle from battle/state context, not weapon dismissal.
    bool prepare_roxas(RC::Unreal::UObject* pawn);
    void prepare_sneeze_finisher(RC::Unreal::UObject* pawn, RC::Unreal::UObject* attack_state, bool airborne);
    void clear_sneeze_finisher();
    RC::Unreal::FName effective_animation_name(RC::Unreal::UObject* animation_instance, RC::Unreal::FName name);
    // Playback applies this receiver-specific stride correction once. Handoffs
    // remove it from the observed source rate before playing another hero.
    float movement_play_rate_scale(RC::Unreal::UObject* animation_instance, RC::Unreal::FName name);
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
    bool diagnostic_battle(bool enabled);
#endif
} // namespace kh3ap::character_idle
