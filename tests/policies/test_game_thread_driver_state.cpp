#include "kh3ap/app/game_thread_driver_state.h"

#include <cassert>
#include <chrono>

int main()
{
    using namespace kh3ap;
    using Clock = std::chrono::steady_clock;

    RuntimeGameThreadDriverState state{};
    assert(!state.unreal_initialized());
    assert(state.note_game_thread(41));
    assert(!state.note_game_thread(41));
    assert(!state.note_game_thread(99));

    const auto initialized_at = Clock::time_point{std::chrono::milliseconds{10}};
    state.begin_unreal_init(initialized_at);
    assert(state.unreal_init_time() == initialized_at);
    assert(!state.try_begin_update(
                    41,
                    initialized_at,
                    std::chrono::milliseconds{5})
                    .should_run);

    state.complete_unreal_init();
    assert(state.unreal_initialized());
    assert(!state.try_begin_update(
                    99,
                    initialized_at,
                    std::chrono::milliseconds{5})
                    .should_run);

    const auto first = state.try_begin_update(
            41,
            initialized_at,
            std::chrono::milliseconds{5});
    assert(first.should_run);
    assert(first.log_first_update);
    assert(first.game_thread_id == 41);
    assert(state.snapshot().update_in_progress);
    assert(!state.try_begin_update(
                    41,
                    initialized_at + std::chrono::milliseconds{5},
                    std::chrono::milliseconds{5})
                    .should_run);

    state.finish_update();
    assert(!state.snapshot().update_in_progress);
    assert(!state.try_begin_update(
                    41,
                    initialized_at + std::chrono::milliseconds{4},
                    std::chrono::milliseconds{5})
                    .should_run);

    const auto second = state.try_begin_update(
            41,
            initialized_at + std::chrono::milliseconds{5},
            std::chrono::milliseconds{5});
    assert(second.should_run);
    assert(!second.log_first_update);
    state.finish_update();

    state.mark_unreal_uninitialized();
    assert(!state.unreal_initialized());
    assert(!state.try_begin_update(
                    41,
                    initialized_at + std::chrono::milliseconds{10},
                    std::chrono::milliseconds{5})
                    .should_run);

    const auto snapshot = state.snapshot();
    assert(snapshot.game_thread_id == 41);
    assert(snapshot.first_update_logged);
    assert(snapshot.last_update ==
           initialized_at + std::chrono::milliseconds{5});

    return 0;
}
