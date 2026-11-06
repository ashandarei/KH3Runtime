#include "kh3ap/features/grants/caribbean_chart_unlock_state.h"

#include <cassert>
#include <chrono>

int main()
{
    using namespace std::chrono_literals;

    kh3ap::RuntimeCaribbeanChartUnlockState state{};
    const auto start = std::chrono::steady_clock::time_point{} + 10s;

    assert(!state.pending());
    assert(!state.ready(start));
    assert(state.arm());
    assert(state.pending());
    assert(!state.ready(start + 30s));
    assert(!state.arm());

    assert(state.mark_map_ready(start, 1s));
    assert(!state.ready(start + 999ms));
    assert(state.ready(start + 1s));
    assert(!state.mark_map_ready(start + 1s, 1s));

    state.record_attempt(false, start + 1s, 2s, 3);
    assert(state.pending());
    assert(state.attempts == 1);
    assert(!state.ready(start + 2999ms));
    assert(state.ready(start + 3s));

    state.record_attempt(false, start + 3s, 2s, 3);
    assert(state.pending());
    assert(state.ready(start + 5s));
    state.record_attempt(true, start + 5s, 2s, 3);
    assert(!state.pending());
    assert(state.phase == kh3ap::RuntimeCaribbeanChartUnlockState::Phase::complete);
    assert(state.attempts == 3);

    state.reset();
    assert(state.phase == kh3ap::RuntimeCaribbeanChartUnlockState::Phase::idle);
    assert(state.attempts == 0);
    assert(state.arm());
    state.reset();
    assert(!state.pending());

    assert(state.arm());
    assert(state.mark_map_ready(start, 0s));
    state.record_attempt(false, start, 1s, 1);
    assert(state.phase == kh3ap::RuntimeCaribbeanChartUnlockState::Phase::failed);
    assert(!state.pending());
    return 0;
}
