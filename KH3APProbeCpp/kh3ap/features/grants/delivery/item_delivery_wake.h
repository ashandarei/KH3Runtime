#pragma once
#include <atomic>
#include <chrono>

namespace kh3ap::item_delivery
{
    // Producers only signal. The game-thread consumer performs all UObject work.
    class DeliveryWakeSchedule
    {
    public:
        auto request() -> void { requested_.store(true, std::memory_order_release); }

        auto should_process(std::chrono::steady_clock::time_point now, bool request_changed) -> bool
        {
            const bool readiness_event = requested_.exchange(false, std::memory_order_acquire);
            if (!request_changed && !readiness_event && (!pending_ || now < next_watchdog_)) return false;
            next_watchdog_ = now + std::chrono::seconds(1);
            return true;
        }
        auto set_pending(bool pending) -> void { pending_ = pending; }

    private:
        std::atomic<bool> requested_{};
        bool pending_{};
        std::chrono::steady_clock::time_point next_watchdog_{};
    };
}
