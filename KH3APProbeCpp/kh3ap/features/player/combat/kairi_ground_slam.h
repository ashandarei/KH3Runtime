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
    bool ready(RC::Unreal::UObject* pawn);
    bool suppress_voice(RC::Unreal::UObject* notify, RC::Unreal::UObject* mesh,
                        RC::Unreal::UObject* animation);
} // namespace kh3ap::kairi_ground_slam
