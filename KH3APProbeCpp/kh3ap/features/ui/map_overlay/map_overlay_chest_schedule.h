#pragma once
#include <chrono>
#include <utility>

namespace kh3ap {
// Chest completion is event-driven. The unrelated emblem/ingredient trackers
// retain their existing visible-map discovery cadence.
class MapOverlayChestSchedule {
public:
    using Clock = std::chrono::steady_clock;
    struct Work { bool discover_other{}, completion{}; };
    auto changed() -> void { dirty_ = true; }
    auto take(Clock::time_point now) -> Work {
        Work work;
        work.discover_other = now >= discovery_;
        work.completion = std::exchange(dirty_,false);
        if (work.discover_other) discovery_ = now + std::chrono::seconds(5);
        return work;
    }
private:
    Clock::time_point discovery_{};
    bool dirty_{true};
};
}
