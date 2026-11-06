#pragma once

#include "kh3ap/features/grants/delivery/manual_runtime_item_grant_state.h"
#include "kh3ap/features/grants/grant_log_throttle_state.h"
#include "kh3ap/features/grants/grant_pawn_state.h"
#include "kh3ap/features/grants/delivery/item_grant_poll_state.h"

#include <chrono>
#include <cstdint>
#include <string>
#include <string_view>

namespace kh3ap
{
    class RuntimeGrantState
    {
    public:
        auto log_throttle_state() -> RuntimeGrantLogThrottleState&;
        auto item_grant_poll_state() -> RuntimeItemGrantPollState&;
        auto pawn_state() -> RuntimeGrantPawnState&;
        auto manual_item_grant_state() -> ManualRuntimeItemGrantState&;
        auto mark_bistro_visited(
                std::wstring_view source,
                std::wstring_view details,
                std::chrono::steady_clock::time_point now) -> void;
        auto mark_bistro_return_waiting_for_map_name(
                std::wstring_view source,
                std::wstring_view details,
                std::chrono::steady_clock::time_point now) -> bool;
        auto clear_bistro_return_gate(
                std::wstring_view source,
                std::chrono::steady_clock::time_point now) -> bool;
        auto mark_classic_kingdom_transition_waiting(
                std::wstring_view source,
                std::wstring_view game_id,
                std::chrono::steady_clock::time_point now) -> bool;
        auto clear_classic_kingdom_transition_gate(
                std::wstring_view source,
                std::chrono::steady_clock::time_point now) -> bool;
        auto mark_olympus_coliseum_transition_waiting(
                std::wstring_view source,
                std::wstring_view cup_id,
                std::chrono::steady_clock::time_point now) -> bool;
        auto clear_olympus_coliseum_transition_gate(
                std::wstring_view source,
                std::chrono::steady_clock::time_point now) -> bool;
        auto update_special_battle_mode(
                bool active,
                std::wstring_view source,
                std::wstring_view details,
                std::chrono::steady_clock::time_point now,
                std::wstring_view map_root = {}) -> bool;
        [[nodiscard]] auto bistro_visited() const -> bool;
        [[nodiscard]] auto bistro_return_waiting_for_map_name() const -> bool;
        [[nodiscard]] auto bistro_gate_source() const -> const std::wstring&;
        [[nodiscard]] auto bistro_gate_details() const -> const std::wstring&;
        [[nodiscard]] auto bistro_gate_wait_ms(std::chrono::steady_clock::time_point now) const -> uint64_t;
        [[nodiscard]] auto classic_kingdom_transition_waiting() const -> bool;
        [[nodiscard]] auto classic_kingdom_gate_source() const -> const std::wstring&;
        [[nodiscard]] auto classic_kingdom_gate_game_id() const -> const std::wstring&;
        [[nodiscard]] auto classic_kingdom_gate_wait_ms(std::chrono::steady_clock::time_point now) const -> uint64_t;
        [[nodiscard]] auto olympus_coliseum_transition_waiting() const -> bool;
        [[nodiscard]] auto olympus_coliseum_gate_source() const -> const std::wstring&;
        [[nodiscard]] auto olympus_coliseum_gate_cup_id() const -> const std::wstring&;
        [[nodiscard]] auto olympus_coliseum_gate_wait_ms(std::chrono::steady_clock::time_point now) const -> uint64_t;
        [[nodiscard]] auto special_battle_mode_active() const -> bool;
        [[nodiscard]] auto special_battle_mode_source() const -> const std::wstring&;
        [[nodiscard]] auto special_battle_mode_details() const -> const std::wstring&;
        [[nodiscard]] auto special_battle_mode_map_root() const -> const std::wstring&;
        [[nodiscard]] auto special_battle_mode_wait_ms(std::chrono::steady_clock::time_point now) const -> uint64_t;

    private:
        RuntimeGrantLogThrottleState log_throttle_state_{};
        RuntimeItemGrantPollState item_grant_poll_state_{};
        RuntimeGrantPawnState pawn_state_{};
        ManualRuntimeItemGrantState manual_item_grant_state_{};
        bool bistro_visited_{false};
        bool bistro_return_waiting_for_map_name_{false};
        std::wstring bistro_gate_source_{};
        std::wstring bistro_gate_details_{};
        std::chrono::steady_clock::time_point bistro_visited_at_{};
        std::chrono::steady_clock::time_point bistro_return_waiting_started_at_{};
        bool classic_kingdom_transition_waiting_{false};
        std::wstring classic_kingdom_gate_source_{};
        std::wstring classic_kingdom_gate_game_id_{};
        std::chrono::steady_clock::time_point classic_kingdom_transition_started_at_{};
        bool olympus_coliseum_transition_waiting_{false};
        std::wstring olympus_coliseum_gate_source_{};
        std::wstring olympus_coliseum_gate_cup_id_{};
        std::chrono::steady_clock::time_point olympus_coliseum_transition_started_at_{};
        bool special_battle_mode_active_{false};
        std::wstring special_battle_mode_source_{};
        std::wstring special_battle_mode_details_{};
        std::wstring special_battle_mode_map_root_{};
        std::chrono::steady_clock::time_point special_battle_mode_started_at_{};
    };
}
