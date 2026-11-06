#pragma once

namespace kh3ap {
// Count construction attempts, not time spent streaming prerequisites. A cold
// world can take longer than this budget to load its assets. Reset only for a
// new world/placement configuration; construction failures remain bounded.
struct RuntimeMusicChestInitialization {
    static constexpr unsigned max_attempts = 30;
    unsigned attempts{};
    bool complete{};

    bool stopped() const { return complete || attempts >= max_attempts; }
    template<class Initialize> void attempt(bool dependencies_ready, Initialize&& initialize) {
        if (stopped() || !dependencies_ready) return;
        ++attempts; // Exceptions consume the budget too.
        complete = initialize();
    }
};
}
