#include "kh3ap/features/player/context/player_reacquire_state.h"

#include <cassert>
#include <chrono>

int main()
{
    using namespace kh3ap;
    using Clock = std::chrono::steady_clock;

    RuntimePlayerReacquireState state{};
    assert(!state.ignored_map_active());
    assert(!state.reacquire_armed());

    const auto armed_at = Clock::time_point{std::chrono::milliseconds{10}};
    state.arm(armed_at);
    assert(state.reacquire_armed());
    assert(!state.should_attempt(
            armed_at + std::chrono::milliseconds{99},
            std::chrono::milliseconds{100}));
    assert(state.should_attempt(
            armed_at + std::chrono::milliseconds{100},
            std::chrono::milliseconds{100}));
    assert(!state.should_attempt(
            armed_at + std::chrono::milliseconds{150},
            std::chrono::milliseconds{100}));

    state.stop_for_invalid_controller();
    assert(!state.reacquire_armed());
    assert(state.snapshot().last_reacquire_attempt ==
           armed_at + std::chrono::milliseconds{100});
    assert(!state.disarm());

    state.arm(armed_at);
    assert(state.enter_ignored_map(
            L"World /Game/Levels/yt/yt_50/yt_50.yt_50"));
    assert(state.ignored_map_active());
    assert(!state.reacquire_armed());
    assert(state.snapshot().last_reacquire_attempt == Clock::time_point{});
    assert(!state.enter_ignored_map(
            L"World /Game/Levels/yt/yt_50/yt_50.yt_50"));
    assert(state.enter_ignored_map(
            L"World /Game/Levels/yt/yt_50/yt_50_Transition"));

    assert(!state.leave_ignored_map_from_validated_pawn(
            L"Pawn /Game/Levels/yt/yt_50/PersistentLevel.Sora"));
    assert(state.ignored_map_active());
    assert(state.leave_ignored_map_from_validated_pawn(
            L"Pawn /Game/Levels/ew/ew_01/PersistentLevel.Sora"));
    assert(!state.ignored_map_active());
    assert(state.snapshot().ignored_map_context.empty());
    assert(!state.leave_ignored_map_from_validated_pawn(
            L"Pawn /Game/Levels/ew/ew_01/PersistentLevel.Sora"));

    state.arm(armed_at);
    assert(state.disarm());
    assert(!state.reacquire_armed());
    assert(state.snapshot().last_reacquire_attempt == Clock::time_point{});

    return 0;
}
