#include "kh3ap/features/player/context/player_reacquire_state.h"

#include "kh3ap/features/grants/ap_ignored_map.h"
#include "kh3ap/bridge/catalog/update_catalog_state.h"

namespace kh3ap
{
    auto RuntimePlayerReacquireState::enter_ignored_map(
            std::wstring_view context) -> bool
    {
        const bool changed =
                !snapshot_.ignored_map_active ||
                snapshot_.ignored_map_context != context;
        snapshot_.ignored_map_active = true;
        snapshot_.ignored_map_context = context;
        suppress();
        return changed;
    }

    auto RuntimePlayerReacquireState::leave_ignored_map_from_validated_pawn(
            std::wstring_view pawn_name) -> bool
    {
        if (!snapshot_.ignored_map_active ||
            is_runtime_ap_ignored_map_context(pawn_name))
        {
            return false;
        }

        snapshot_.ignored_map_active = false;
        snapshot_.ignored_map_context.clear();
        return true;
    }

    auto RuntimePlayerReacquireState::arm(
            std::chrono::steady_clock::time_point now) -> void
    {
        snapshot_.reacquire_armed = true;
        snapshot_.last_reacquire_attempt = now;
    }

    auto RuntimePlayerReacquireState::disarm() -> bool
    {
        if (!snapshot_.reacquire_armed)
        {
            return false;
        }
        suppress();
        return true;
    }

    auto RuntimePlayerReacquireState::suppress() -> void
    {
        snapshot_.reacquire_armed = false;
        snapshot_.last_reacquire_attempt = {};
    }

    auto RuntimePlayerReacquireState::stop_for_invalid_controller() -> void
    {
        // Preserve the last attempt timestamp to match the legacy lifecycle;
        // the disarmed state makes it observational only.
        snapshot_.reacquire_armed = false;
    }

    auto RuntimePlayerReacquireState::should_attempt(
            std::chrono::steady_clock::time_point now,
            std::chrono::steady_clock::duration interval) -> bool
    {
        return snapshot_.reacquire_armed &&
               should_run_periodic_update(
                       snapshot_.last_reacquire_attempt,
                       now,
                       interval);
    }

    auto RuntimePlayerReacquireState::ignored_map_active() const -> bool
    {
        return snapshot_.ignored_map_active;
    }

    auto RuntimePlayerReacquireState::reacquire_armed() const -> bool
    {
        return snapshot_.reacquire_armed;
    }

    auto RuntimePlayerReacquireState::snapshot() const
            -> RuntimePlayerReacquireSnapshot
    {
        return snapshot_;
    }
}
