#pragma once
#include <chrono>

namespace kh3ap {
// Game-thread work latch. Only unfinished operations retain a deadline;
// events raised while processing must survive that attempt's completion.
class ChestMaintenanceWork {
public:
    using Clock = std::chrono::steady_clock;
    void request() { pending_ = true; next_ = {}; }
    bool pending() const { return pending_; }
    bool take(Clock::time_point now) {
        if (!pending_ || now < next_) return false;
        pending_ = false;
        return true;
    }
    void finish(Clock::time_point now, bool complete) {
        if (!complete && !pending_) {
            pending_ = true;
            next_ = now + std::chrono::seconds(1);
        }
    }
private:
    bool pending_{true};
    Clock::time_point next_{};
};
}
