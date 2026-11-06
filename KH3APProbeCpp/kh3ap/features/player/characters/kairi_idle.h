#pragma once

namespace RC::Unreal
{
    class UObject;
    struct FName;
}

namespace kh3ap::kairi_idle
{
    // Selection-time setup; leaves live pawns and animation ownership alone.
    bool initialize();
    // Called only while a character swap/rebind is unfinished. False means
    // the installed animation set is still loading; no idle polling is needed.
    bool prepare(RC::Unreal::UObject* pawn);
    RC::Unreal::FName effective_animation_name(RC::Unreal::UObject* animation_instance, RC::Unreal::FName name);
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
    bool diagnostic_battle(bool enabled);
#endif
} // namespace kh3ap::kairi_idle
