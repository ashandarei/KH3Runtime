#include "kh3ap/features/story/cutscenes/shoot_flow_restore_state.h"

#include <cassert>

int main()
{
    using Observation = kh3ap::RuntimeShootFlowRestoreObservation;
    using namespace std::chrono_literals;
    kh3ap::RuntimeShootFlowRestoreState state{};
    const auto start = std::chrono::steady_clock::time_point{} + 10s;

    assert(state.observe_pause_ui(true) == Observation::Ignored);
    assert(state.observe_pause_ui(false) == Observation::Ignored);
    assert(state.observe_special_battle(false) == Observation::Ignored);
    assert(!state.pending());

    assert(state.observe_special_battle(true) == Observation::SpecialBattleArmed);
    assert(state.observe_special_battle(true) == Observation::Ignored);
    assert(state.observe_pause_ui(true) == Observation::TutorialOpened);
    assert(state.observe_pause_ui(true) == Observation::Ignored);
    assert(state.observe_pause_ui(false) == Observation::TutorialReleased);
    assert(!state.pending());
    assert(state.observe_player_state(false, true, start + 100ms, 250ms) == Observation::Ignored);
    assert(state.observe_player_state(true, true, start + 500ms, 250ms) == Observation::PostTutorialCinematicSeen);
    assert(state.observe_player_state(true, true, start + 600ms, 250ms) == Observation::Ignored);
    assert(state.observe_player_state(false, true, start + 1s, 250ms) == Observation::RestoreQueued);
    assert(state.pending());
    assert(!state.ready(start + 1249ms));
    assert(state.ready(start + 1250ms));
    assert(state.observe_pause_ui(true) == Observation::Ignored);

    assert(!state.consume_pending(start + 1249ms));
    assert(state.consume_pending(start + 1250ms));
    assert(!state.pending());
    assert(state.consumed_for_pawn());
    assert(!state.consume_pending(start + 1250ms));
    state.record_restore_result(true);

    assert(state.observe_pause_ui(true) == Observation::Ignored);
    assert(state.observe_pause_ui(false) == Observation::Ignored);
    assert(state.observe_special_battle(true) == Observation::Ignored);
    assert(state.special_battle_count() == 1);
    assert(state.tutorial_open_count() == 1);
    assert(state.tutorial_release_count() == 1);
    assert(state.post_tutorial_cinematic_count() == 1);
    assert(state.cinematic_exit_queue_count() == 1);
    assert(state.restore_attempt_count() == 1);
    assert(state.restore_success_count() == 1);

    state.reset_for_pawn();
    assert(!state.pending());
    assert(!state.consumed_for_pawn());
    assert(state.observe_special_battle(true) == Observation::SpecialBattleArmed);
    assert(state.observe_pause_ui(true) == Observation::TutorialOpened);
    assert(state.observe_pause_ui(false) == Observation::TutorialReleased);
    assert(state.observe_player_state(false, false, start + 8s, 250ms) == Observation::Ignored);
    assert(state.observe_player_state(false, true, start + 20s, 250ms) == Observation::Ignored);
    assert(!state.pending());
    assert(state.observe_player_state(true, true, start + 21s, 250ms) == Observation::PostTutorialCinematicSeen);
    assert(state.observe_player_state(false, true, start + 22s, 250ms) == Observation::RestoreQueued);
    assert(!state.consume_pending(start + 22249ms));
    assert(state.consume_pending(start + 22250ms));
    assert(state.special_battle_count() == 2);
    assert(state.tutorial_open_count() == 2);
    assert(state.tutorial_release_count() == 2);
    assert(state.post_tutorial_cinematic_count() == 2);
    assert(state.cinematic_exit_queue_count() == 2);
    assert(state.restore_attempt_count() == 2);

    return 0;
}
