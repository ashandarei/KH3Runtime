#pragma once

#include <Unreal/FWeakObjectPtr.hpp>

#include <chrono>
#include <string>
#include <vector>

namespace RC::Unreal
{
    class UObject;
}

namespace kh3ap
{
    struct RuntimeColorPendingPawn
    {
        RC::Unreal::FWeakObjectPtr pawn{};
        std::wstring character_key{};
        // When this pawn was first queued. A companion pawn is only just
        // spawned when detected (unlike Sora, who has usually been present
        // for a while) - its own mesh/anim setup may not have finished yet
        // even though the pointer is already valid, so callers should wait
        // out a short per-pawn settle delay from this timestamp before
        // touching it.
        std::chrono::steady_clock::time_point queued_at{};
    };

    // Per-process bookkeeping for the native color trigger: which pawns have
    // already been colored (reset on every map jump, since travel always
    // hands us a new pawn instance) and which are still waiting on a safe
    // window to color. Colors themselves are derived from the seed at apply
    // time, so there is no generated data to track here.
    class RuntimeColorApplyState
    {
    public:
        auto reset_for_map_jump() -> void;
        auto prune_expired() -> void;

        [[nodiscard]] auto is_colored(RC::Unreal::UObject* pawn) const -> bool;
        // The pawns coloured since the last reset, so switching the
        // feature off can put their materials back instead of merely
        // declining to paint the next character. Callers must revalidate:
        // a pawn can be destroyed between being coloured and being read
        // back here.
        [[nodiscard]] auto colored() const -> std::vector<RC::Unreal::UObject*>;
        auto mark_colored(RC::Unreal::UObject* pawn) -> void;

        // Weapon actors are recreated on swaps and summons. Weak references
        // retain their serial identity, so a recycled address is a new weapon.
        [[nodiscard]] auto is_weapon_colored(RC::Unreal::UObject* weapon) const -> bool;
        // Same contract as colored(): callers must revalidate, and more
        // so here - a weapon actor is destroyed the moment Sora dismisses
        // or swaps it.
        [[nodiscard]] auto colored_weapons() const -> std::vector<RC::Unreal::UObject*>;
        auto mark_weapon_colored(RC::Unreal::UObject* weapon) -> void;

        [[nodiscard]] auto is_pending(RC::Unreal::UObject* pawn) const -> bool;
        auto queue_pending(
                RC::Unreal::UObject* pawn,
                std::wstring_view character_key,
                std::chrono::steady_clock::time_point now) -> void;
        [[nodiscard]] auto pending() -> std::vector<RuntimeColorPendingPawn>&;

    private:
        std::vector<RC::Unreal::FWeakObjectPtr> colored_pawns_{};
        std::vector<RC::Unreal::FWeakObjectPtr> colored_weapons_{};
        std::vector<RuntimeColorPendingPawn> pending_{};
    };
}
