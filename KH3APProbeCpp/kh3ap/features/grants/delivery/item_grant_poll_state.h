#pragma once
#include "kh3ap/features/grants/delivery/item_delivery_binding.h"
#include "kh3ap/features/grants/delivery/item_delivery_wake.h"
#include "kh3ap/features/grants/delivery/item_delivery_queue.h"

#include <chrono>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_set>

namespace kh3ap
{
    class RuntimeItemGrantPollState
    {
    public:
        auto delivery_wake_schedule() -> item_delivery::DeliveryWakeSchedule& { return delivery_wake_schedule_; }
        auto delivery_queue(bool priority = false) -> item_delivery::DeliveryQueue& { return priority ? priority_queue_ : delivery_queue_; }
        auto scheduled_queue() -> item_delivery::DeliveryQueue& { return scheduled_queue_; }
        auto try_acquire_process_lock() -> std::unique_lock<std::mutex>;

        auto arm(std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now(), bool readiness_event = true) -> void;
        auto arm_ready(
                std::chrono::steady_clock::duration delay,
                std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now()) -> void;
        auto disarm() -> void;
        [[nodiscard]] auto is_armed() const -> bool;
        [[nodiscard]] auto ready_to_process(
                std::chrono::steady_clock::duration delay,
                std::chrono::steady_clock::time_point now) const -> bool;
        [[nodiscard]] auto armed_wait(std::chrono::steady_clock::time_point now) const -> std::chrono::steady_clock::duration;

        auto mark_seen(std::chrono::steady_clock::time_point now) -> void;
        auto mark_applied(std::wstring key, std::chrono::steady_clock::time_point now) -> void;
        [[nodiscard]] auto is_duplicate(std::wstring_view key) const -> bool;
        auto remember_fresh_delivery_key(std::wstring key) -> void;
        [[nodiscard]] auto has_fresh_delivery_key(std::wstring_view key) const -> bool;
        auto clear_last_key_if_stale(
                std::chrono::steady_clock::time_point now,
                std::chrono::steady_clock::duration stale_after) -> bool;
        auto mark_wait_signature(std::wstring signature) -> bool;
        auto clear_wait_signature() -> void;

        auto begin_apply(std::wstring key) -> void;
        auto end_apply() -> void;
        [[nodiscard]] auto apply_in_progress() const -> bool;
        [[nodiscard]] auto apply_in_progress_key() const -> const std::wstring&;

    private:
        item_delivery::DeliveryWakeSchedule delivery_wake_schedule_;
        item_delivery::DeliveryQueue delivery_queue_, priority_queue_;
        item_delivery::DeliveryQueue scheduled_queue_;
        std::mutex process_mutex_{};
        bool armed_{false};
        std::chrono::steady_clock::time_point armed_at_{};
        std::wstring last_key_{};
        std::wstring last_wait_signature_{};
        std::unordered_set<std::wstring> fresh_delivery_keys_{};
        std::chrono::steady_clock::time_point last_seen_at_{};
        bool apply_in_progress_{false};
        std::wstring apply_in_progress_key_{};
    };
}
