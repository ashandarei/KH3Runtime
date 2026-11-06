#include "kh3ap/features/kh02/story/kh02_map_session_state.h"
#include "kh3ap/features/kh02/cutscenes/kh02_cutscene_session_state.h"
#include <cassert>
#include <stdexcept>
#include <string_view>
#include <vector>

using kh3ap::Kh02ArrivalPhase;
using kh3ap::Kh02MapSessionState;

int main()
{
    // Loading retries and active re-arms share one release; fresh arrivals do
    // their own work. A release that throws must not commit the once-only gate.
    Kh02MapSessionState loading;
    int menu_releases{};
    loading.arm();
    try
    {
        loading.release_menu_music_once([] { throw std::runtime_error("release_failed"); });
        assert(false);
    }
    catch (const std::runtime_error&)
    {
    }
    const auto release_menu = [&] { ++menu_releases; };
    loading.release_menu_music_once(release_menu);
    loading.release_menu_music_once(release_menu);
    loading.arm();
    loading.replace_source();
    loading.release_menu_music_once(release_menu);
    assert(menu_releases == 1);
    assert(loading.transition(Kh02ArrivalPhase::Idle));
    loading.arm();
    loading.release_menu_music_once(release_menu);
    assert(menu_releases == 2);
    assert(loading.transition(Kh02ArrivalPhase::Error));
    loading.arm();
    loading.release_menu_music_once(release_menu);
    assert(menu_releases == 3);
    assert(loading.transition(Kh02ArrivalPhase::ClosingLoading));
    assert(loading.transition(Kh02ArrivalPhase::Fading));
    assert(loading.transition(Kh02ArrivalPhase::Completed));
    loading.arm();
    loading.release_menu_music_once(release_menu);
    assert(menu_releases == 4);

    using kh3ap::Kh02ArrivalInput;
    using kh3ap::Kh02ArrivalInputClaims;
    Kh02ArrivalInputClaims claims{true, true};
    std::vector<Kh02ArrivalInput> releases;
    try
    {
        claims.release(
            [&](Kh02ArrivalInput input)
            {
                releases.push_back(input);
                if (input == Kh02ArrivalInput::Movement)
                {
                    throw std::runtime_error("move_release_failed");
                }
            });
        assert(false);
    }
    catch (const std::runtime_error& error)
    {
        assert(std::string_view(error.what()) == "move_release_failed");
    }
    // Failure to release movement cannot strand the independently owned camera
    // claim. Retrying cleanup must not decrement that camera counter twice.
    assert(releases == std::vector({Kh02ArrivalInput::Movement, Kh02ArrivalInput::Look}));
    assert(claims.movement && !claims.look);
    releases.clear();
    claims.release([&](Kh02ArrivalInput input) { releases.push_back(input); });
    assert(releases == std::vector({Kh02ArrivalInput::Movement}));
    assert(!claims.movement && !claims.look);
    claims = {true, true};
    try
    {
        claims.release([](Kh02ArrivalInput input)
                       { throw std::runtime_error(input == Kh02ArrivalInput::Movement ? "first" : "second"); });
        assert(false);
    }
    catch (const std::runtime_error& error)
    {
        assert(std::string_view(error.what()) == "first");
    }
    assert(claims.movement && claims.look);

    Kh02MapSessionState session;
    assert(!session.active());

    using kh3ap::kh02_held_matinee_action;
    using kh3ap::Kh02HeldMatineeAction;
    // A held ending waits for real movie EOF; pause alone cannot complete it.
    assert(kh02_held_matinee_action(true, true, true, false, false) == Kh02HeldMatineeAction::None);
    assert(kh02_held_matinee_action(true, true, true, true, false) == Kh02HeldMatineeAction::Resume);
    // Retry/cancel retires that exact owned scene even if its pause was released.
    assert(kh02_held_matinee_action(true, true, true, true, true) == Kh02HeldMatineeAction::Retire);
    assert(kh02_held_matinee_action(true, true, false, false, true) == Kh02HeldMatineeAction::Retire);
    // A user/native pause and an already stopped scene are not ours to resume.
    assert(kh02_held_matinee_action(false, true, true, true, true) == Kh02HeldMatineeAction::None);
    assert(kh02_held_matinee_action(true, false, true, true, true) == Kh02HeldMatineeAction::None);
    assert(!session.accepts(0));

    // A normal entry cannot release its cover before native loading settles.
    session.replace_source();
    const auto first_world = session.generation();
    session.arm();
    assert(!session.transition(Kh02ArrivalPhase::Completed));
    assert(!session.transition(Kh02ArrivalPhase::Fading));
    assert(session.transition(Kh02ArrivalPhase::ClosingLoading));
    assert(session.transition(Kh02ArrivalPhase::Fading));
    assert(session.active());
    assert(session.transition(Kh02ArrivalPhase::Completed));
    assert(!session.active());
    assert(session.accepts(first_world));
    assert(session.accepts_continuation(first_world, true));
    // A still-valid old actor/world is insufficient during travel. No verified
    // current world also leaves the callback ineligible until ownership returns.
    assert(!session.accepts_continuation(first_world, false));

    // Retry changes script lifetime without requiring a new host-world pointer.
    session.replace_source();
    const auto retry = session.generation();
    assert(retry != first_world);
    assert(!session.accepts(first_world));
    assert(session.accepts(retry));
    assert(!session.accepts_continuation(first_world, true));
    assert(!session.accepts_continuation(first_world, false));
    assert(session.accepts_continuation(retry, true));
    session.arm();
    assert(session.transition(Kh02ArrivalPhase::ClosingLoading));
    assert(session.transition(Kh02ArrivalPhase::Opening));
    assert(!session.transition(Kh02ArrivalPhase::Completed));
    assert(session.transition(Kh02ArrivalPhase::Fading));
    assert(session.transition(Kh02ArrivalPhase::Completed));

    // Cancellation/error retires an active phase and allows an explicit rearm.
    session.arm();
    assert(session.transition(Kh02ArrivalPhase::Error));
    assert(!session.active());
    assert(session.phase_name() == "error");
    assert(session.transition(Kh02ArrivalPhase::Idle));
    session.replace_source();
    assert(!session.accepts(retry));
    session.arm();
    assert(session.phase_name() == "loading");
    assert(session.transition(Kh02ArrivalPhase::Idle));
    assert(!session.active());
}
