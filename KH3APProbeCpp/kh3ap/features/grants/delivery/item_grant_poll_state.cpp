#include "kh3ap/features/grants/delivery/item_grant_poll_state.h"

#include <utility>

namespace kh3ap
{
    auto RuntimeItemGrantPollState::try_acquire_process_lock() -> std::unique_lock<std::mutex>
    {
        return std::unique_lock<std::mutex>{process_mutex_, std::try_to_lock};
    }

    auto RuntimeItemGrantPollState::arm(std::chrono::steady_clock::time_point now, bool readiness_event) -> void
    {
        armed_ = true;
        armed_at_ = now;
        if (readiness_event) delivery_wake_schedule_.request();
    }

    auto RuntimeItemGrantPollState::arm_ready(
            std::chrono::steady_clock::duration delay,
            std::chrono::steady_clock::time_point now) -> void
    {
        armed_ = true;
        armed_at_ = now - delay;
        delivery_wake_schedule_.request();
    }

    auto RuntimeItemGrantPollState::disarm() -> void
    {
        armed_ = false;
        armed_at_ = {};
        clear_wait_signature();
    }

    auto RuntimeItemGrantPollState::is_armed() const -> bool
    {
        return armed_;
    }

    auto RuntimeItemGrantPollState::ready_to_process(
            std::chrono::steady_clock::duration delay,
            std::chrono::steady_clock::time_point now) const -> bool
    {
        return armed_at_.time_since_epoch().count() != 0 && (now - armed_at_) >= delay;
    }

    auto RuntimeItemGrantPollState::armed_wait(std::chrono::steady_clock::time_point now) const
            -> std::chrono::steady_clock::duration
    {
        return armed_at_.time_since_epoch().count() == 0 ? std::chrono::steady_clock::duration{} : now - armed_at_;
    }

    auto RuntimeItemGrantPollState::mark_seen(std::chrono::steady_clock::time_point now) -> void
    {
        last_seen_at_ = now;
    }

    auto RuntimeItemGrantPollState::mark_applied(std::wstring key, std::chrono::steady_clock::time_point now) -> void
    {
        last_key_ = std::move(key);
        last_seen_at_ = now;
        clear_wait_signature();
    }

    auto RuntimeItemGrantPollState::is_duplicate(std::wstring_view key) const -> bool
    {
        return key == last_key_;
    }

    auto RuntimeItemGrantPollState::remember_fresh_delivery_key(std::wstring key) -> void
    {
        if (!key.empty())
        {
            fresh_delivery_keys_.insert(std::move(key));
        }
    }

    auto RuntimeItemGrantPollState::has_fresh_delivery_key(std::wstring_view key) const -> bool
    {
        return !key.empty() && fresh_delivery_keys_.contains(std::wstring{key});
    }

    auto RuntimeItemGrantPollState::clear_last_key_if_stale(
            std::chrono::steady_clock::time_point now,
            std::chrono::steady_clock::duration stale_after) -> bool
    {
        if (!last_key_.empty() &&
            last_seen_at_.time_since_epoch().count() != 0 &&
            (now - last_seen_at_) >= stale_after)
        {
            last_key_.clear();
            last_seen_at_ = {};
            return true;
        }
        return false;
    }

    auto RuntimeItemGrantPollState::mark_wait_signature(std::wstring signature) -> bool
    {
        if (signature == last_wait_signature_)
        {
            return false;
        }

        last_wait_signature_ = std::move(signature);
        return true;
    }

    auto RuntimeItemGrantPollState::clear_wait_signature() -> void
    {
        last_wait_signature_.clear();
    }

    auto RuntimeItemGrantPollState::begin_apply(std::wstring key) -> void
    {
        apply_in_progress_ = true;
        apply_in_progress_key_ = std::move(key);
    }

    auto RuntimeItemGrantPollState::end_apply() -> void
    {
        apply_in_progress_ = false;
        apply_in_progress_key_.clear();
    }

    auto RuntimeItemGrantPollState::apply_in_progress() const -> bool
    {
        return apply_in_progress_;
    }

    auto RuntimeItemGrantPollState::apply_in_progress_key() const -> const std::wstring&
    {
        return apply_in_progress_key_;
    }
}
