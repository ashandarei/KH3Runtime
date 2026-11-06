#include "kh3ap/features/grants/grant_state.h"

#include <chrono>

namespace kh3ap
{
    auto RuntimeGrantState::log_throttle_state() -> RuntimeGrantLogThrottleState&
    {
        return log_throttle_state_;
    }

    auto RuntimeGrantState::item_grant_poll_state() -> RuntimeItemGrantPollState&
    {
        return item_grant_poll_state_;
    }

    auto RuntimeGrantState::pawn_state() -> RuntimeGrantPawnState&
    {
        return pawn_state_;
    }

    auto RuntimeGrantState::manual_item_grant_state() -> ManualRuntimeItemGrantState&
    {
        return manual_item_grant_state_;
    }

    auto RuntimeGrantState::mark_bistro_visited(
            std::wstring_view source,
            std::wstring_view details,
            std::chrono::steady_clock::time_point now) -> void
    {
        bistro_visited_ = true;
        bistro_return_waiting_for_map_name_ = false;
        bistro_gate_source_ = std::wstring{source};
        bistro_gate_details_ = std::wstring{details};
        bistro_visited_at_ = now;
        bistro_return_waiting_started_at_ = {};
    }

    auto RuntimeGrantState::mark_bistro_return_waiting_for_map_name(
            std::wstring_view source,
            std::wstring_view details,
            std::chrono::steady_clock::time_point now) -> bool
    {
        if (!bistro_visited_)
        {
            return false;
        }

        const bool changed = !bistro_return_waiting_for_map_name_;
        bistro_return_waiting_for_map_name_ = true;
        bistro_gate_source_ = std::wstring{source};
        bistro_gate_details_ = std::wstring{details};
        bistro_return_waiting_started_at_ = now;
        return changed;
    }

    auto RuntimeGrantState::clear_bistro_return_gate(
            std::wstring_view source,
            std::chrono::steady_clock::time_point now) -> bool
    {
        (void)source;
        (void)now;
        const bool changed = bistro_visited_ || bistro_return_waiting_for_map_name_;
        bistro_visited_ = false;
        bistro_return_waiting_for_map_name_ = false;
        bistro_gate_source_.clear();
        bistro_gate_details_.clear();
        bistro_visited_at_ = {};
        bistro_return_waiting_started_at_ = {};
        return changed;
    }

    auto RuntimeGrantState::mark_classic_kingdom_transition_waiting(
            std::wstring_view source,
            std::wstring_view game_id,
            std::chrono::steady_clock::time_point now) -> bool
    {
        const bool changed = !classic_kingdom_transition_waiting_;
        classic_kingdom_transition_waiting_ = true;
        classic_kingdom_gate_source_ = std::wstring{source};
        classic_kingdom_gate_game_id_ = std::wstring{game_id};
        classic_kingdom_transition_started_at_ = now;
        return changed;
    }

    auto RuntimeGrantState::clear_classic_kingdom_transition_gate(
            std::wstring_view source,
            std::chrono::steady_clock::time_point now) -> bool
    {
        (void)source;
        (void)now;
        if (!classic_kingdom_transition_waiting_)
        {
            return false;
        }
        classic_kingdom_transition_waiting_ = false;
        classic_kingdom_gate_source_.clear();
        classic_kingdom_gate_game_id_.clear();
        classic_kingdom_transition_started_at_ = {};
        return true;
    }

    auto RuntimeGrantState::mark_olympus_coliseum_transition_waiting(
            std::wstring_view source,
            std::wstring_view cup_id,
            std::chrono::steady_clock::time_point now) -> bool
    {
        const bool changed = !olympus_coliseum_transition_waiting_;
        olympus_coliseum_transition_waiting_ = true;
        olympus_coliseum_gate_source_ = std::wstring{source};
        olympus_coliseum_gate_cup_id_ = std::wstring{cup_id};
        olympus_coliseum_transition_started_at_ = now;
        return changed;
    }

    auto RuntimeGrantState::clear_olympus_coliseum_transition_gate(
            std::wstring_view source,
            std::chrono::steady_clock::time_point now) -> bool
    {
        (void)source;
        (void)now;
        if (!olympus_coliseum_transition_waiting_)
        {
            return false;
        }
        olympus_coliseum_transition_waiting_ = false;
        olympus_coliseum_gate_source_.clear();
        olympus_coliseum_gate_cup_id_.clear();
        olympus_coliseum_transition_started_at_ = {};
        return true;
    }

    auto RuntimeGrantState::update_special_battle_mode(
            bool active,
            std::wstring_view source,
            std::wstring_view details,
            std::chrono::steady_clock::time_point now,
            std::wstring_view map_root) -> bool
    {
        const bool changed = special_battle_mode_active_ != active;
        special_battle_mode_active_ = active;
        special_battle_mode_source_ = std::wstring{source};
        special_battle_mode_details_ = std::wstring{details};
        special_battle_mode_map_root_ = std::wstring{map_root};
        special_battle_mode_started_at_ = active ? now : std::chrono::steady_clock::time_point{};
        return changed;
    }

    auto RuntimeGrantState::bistro_visited() const -> bool
    {
        return bistro_visited_;
    }

    auto RuntimeGrantState::bistro_return_waiting_for_map_name() const -> bool
    {
        return bistro_return_waiting_for_map_name_;
    }

    auto RuntimeGrantState::bistro_gate_source() const -> const std::wstring&
    {
        return bistro_gate_source_;
    }

    auto RuntimeGrantState::bistro_gate_details() const -> const std::wstring&
    {
        return bistro_gate_details_;
    }

    auto RuntimeGrantState::bistro_gate_wait_ms(std::chrono::steady_clock::time_point now) const -> uint64_t
    {
        if (bistro_return_waiting_started_at_.time_since_epoch().count() == 0)
        {
            return 0;
        }
        return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                             now - bistro_return_waiting_started_at_)
                                             .count());
    }

    auto RuntimeGrantState::classic_kingdom_transition_waiting() const -> bool
    {
        return classic_kingdom_transition_waiting_;
    }

    auto RuntimeGrantState::classic_kingdom_gate_source() const -> const std::wstring&
    {
        return classic_kingdom_gate_source_;
    }

    auto RuntimeGrantState::classic_kingdom_gate_game_id() const -> const std::wstring&
    {
        return classic_kingdom_gate_game_id_;
    }

    auto RuntimeGrantState::classic_kingdom_gate_wait_ms(
            std::chrono::steady_clock::time_point now) const -> uint64_t
    {
        if (classic_kingdom_transition_started_at_.time_since_epoch().count() == 0)
        {
            return 0;
        }
        return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                             now - classic_kingdom_transition_started_at_)
                                             .count());
    }

    auto RuntimeGrantState::olympus_coliseum_transition_waiting() const -> bool
    {
        return olympus_coliseum_transition_waiting_;
    }

    auto RuntimeGrantState::olympus_coliseum_gate_source() const
            -> const std::wstring&
    {
        return olympus_coliseum_gate_source_;
    }

    auto RuntimeGrantState::olympus_coliseum_gate_cup_id() const
            -> const std::wstring&
    {
        return olympus_coliseum_gate_cup_id_;
    }

    auto RuntimeGrantState::olympus_coliseum_gate_wait_ms(
            std::chrono::steady_clock::time_point now) const -> uint64_t
    {
        if (olympus_coliseum_transition_started_at_.time_since_epoch().count() == 0)
        {
            return 0;
        }
        return static_cast<uint64_t>(
                std::chrono::duration_cast<std::chrono::milliseconds>(
                        now - olympus_coliseum_transition_started_at_)
                        .count());
    }

    auto RuntimeGrantState::special_battle_mode_active() const -> bool
    {
        return special_battle_mode_active_;
    }

    auto RuntimeGrantState::special_battle_mode_source() const -> const std::wstring&
    {
        return special_battle_mode_source_;
    }

    auto RuntimeGrantState::special_battle_mode_details() const -> const std::wstring&
    {
        return special_battle_mode_details_;
    }

    auto RuntimeGrantState::special_battle_mode_map_root() const -> const std::wstring&
    {
        return special_battle_mode_map_root_;
    }

    auto RuntimeGrantState::special_battle_mode_wait_ms(std::chrono::steady_clock::time_point now) const -> uint64_t
    {
        if (special_battle_mode_started_at_.time_since_epoch().count() == 0)
        {
            return 0;
        }
        return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                             now - special_battle_mode_started_at_)
                                             .count());
    }
}
