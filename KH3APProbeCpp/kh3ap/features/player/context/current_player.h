#pragma once
#include <cstdint>
namespace RC::Unreal { class UObject; }
namespace kh3ap
{
    // Game-thread-only identity, populated by possession/start/swap events.
    // Rejected NPC/stale observations never replace the local player.
    bool runtime_observe_current_player(RC::Unreal::UObject* pawn);
    void runtime_clear_current_player();
    std::uint64_t runtime_current_player_revision();
    // Reads follow the recorded LocalPlayer's actual possession. No Sora fallback.
    // Does not authorize AP grants or access to Sora's equipment/ability banks.
    RC::Unreal::UObject* runtime_current_player_pawn();
    RC::Unreal::UObject* runtime_ready_player_pawn();
}
