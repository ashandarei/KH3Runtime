#pragma once
#include <cstdint>
#include <optional>

namespace RC::Unreal { class UObject; }

namespace kh3ap::character_exploration
{
    bool prepare();
    bool prepare_pawn(RC::Unreal::UObject* pawn);
    void refresh_portrait(RC::Unreal::UObject* pawn);
    void refresh_unlocks(RC::Unreal::UObject* hero, bool mp_enabled);
    // A completed session may finish inventory work after returning to Idle.
    // Its caller must retain the actual shop participant with a weak pointer.
    RC::Unreal::UObject* shop_context(RC::Unreal::UObject* completed_shop_pawn = nullptr);
    std::optional<bool> observe_shop_state_write(uintptr_t component, uintptr_t state);
}
