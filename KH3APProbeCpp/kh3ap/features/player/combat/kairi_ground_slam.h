#pragma once

namespace RC::Unreal
{
    class UObject;
}

namespace kh3ap::kairi_ground_slam
{
    void observe_gimmick_damage(RC::Unreal::UObject* component, RC::Unreal::UObject* controller,
                                const void* damage_info, RC::Unreal::UObject* causer);
    // Install shared support during selection, before the first live swap.
    bool initialize();
    // Retries are owned by the unfinished swap/rebind, never an idle timer.
    bool prepare(RC::Unreal::UObject* pawn);
} // namespace kh3ap::kairi_ground_slam
