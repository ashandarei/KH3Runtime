#include "kh3ap/bridge/catalog/update_catalog_state.h"

namespace kh3ap
{
    auto has_elapsed_since(
            std::chrono::steady_clock::time_point start,
            std::chrono::steady_clock::time_point now,
            std::chrono::steady_clock::duration delay) -> bool
    {
        return start.time_since_epoch().count() != 0 && (now - start) >= delay;
    }

    auto should_run_periodic_update(
            std::chrono::steady_clock::time_point& last_run_at,
            std::chrono::steady_clock::time_point now,
            std::chrono::steady_clock::duration interval) -> bool
    {
        if (interval.count() != 0 &&
            last_run_at.time_since_epoch().count() != 0 &&
            (now - last_run_at) < interval)
        {
            return false;
        }

        last_run_at = now;
        return true;
    }

    auto UpdateHeartbeatState::increment_calls() -> uint64_t
    {
        return ++calls_;
    }

    auto UpdateHeartbeatState::calls() const -> uint64_t
    {
        return calls_;
    }

    auto UpdateHeartbeatState::should_check(
            std::chrono::steady_clock::time_point now,
            std::chrono::steady_clock::duration interval) -> bool
    {
        if (last_check_at_.time_since_epoch().count() != 0 && (now - last_check_at_) < interval)
        {
            return false;
        }
        last_check_at_ = now;
        return true;
    }

    auto UpdateHeartbeatState::should_log(
            std::chrono::steady_clock::time_point now,
            std::chrono::steady_clock::duration interval) -> bool
    {
        if (last_log_at_.time_since_epoch().count() != 0 && (now - last_log_at_) < interval)
        {
            return false;
        }
        last_log_at_ = now;
        return true;
    }

}
