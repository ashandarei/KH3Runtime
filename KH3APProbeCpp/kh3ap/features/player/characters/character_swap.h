#pragma once
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include "kh3ap/core/performance_build.h"

namespace RC::Unreal
{
    class UObject;
}

namespace kh3ap
{
    class RuntimeOptionsPageBuilder;

    namespace character_swap
    {
        void append_menu(RuntimeOptionsPageBuilder& page);
        bool route(int controller, std::wstring_view key, int event, bool gamepad);
        // Returns true once a newly possessed alternate is ready for shared unlock refresh.
        bool tick(std::chrono::steady_clock::time_point now);
        RC::Unreal::UObject* current_hero();
        // Party ownership only, including the synchronous incoming factory pawn.
        RC::Unreal::UObject* party_owner();
        RC::Unreal::UObject* reward_context();
        RC::Unreal::UObject* bad_status_context();
        RC::Unreal::UObject* game_over_context();
        void observe_game_over_state_write(uintptr_t component, uintptr_t state);
        void notify_player_started();
        void notify_map_jump(RC::Unreal::UObject* source, std::wstring_view caller);
        bool prepare_native_story_travel();
        RC::Unreal::UObject* return_to_sora_for_encounter(RC::Unreal::UObject* pawn);
        RC::Unreal::UObject* controlled_travel_pawn();
        void notify_cinematic_started(RC::Unreal::UObject* source);
        void notify_cinematic_finished(RC::Unreal::UObject* source);
        void apply_stat_bonus(int hp_amount, int mp_amount);
        bool start_king_mickey_trap(RC::Unreal::UObject* pawn, int64_t receipt_index = -1);
        bool manual_swaps_blocked();
        bool handoff_pending();
        bool enabled();
        bool owns_swap_pawn(RC::Unreal::UObject* pawn);
        bool swap_available(RC::Unreal::UObject* pawn);
        int swap_target(RC::Unreal::UObject* pawn);
        bool dispatch_swap(RC::Unreal::UObject* pawn, int target);
        bool preparation_pending(int target);
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
        bool diagnostic_select(int player_id, bool preload = true);
#endif
    } // namespace character_swap

    // Shared ownership-only grant context; never an alternate's equipment bank.
    RC::Unreal::UObject* persistent_player_reward_context();
} // namespace kh3ap
