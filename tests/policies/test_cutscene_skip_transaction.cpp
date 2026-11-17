#include "kh3ap/features/story/cutscenes/cutscene_skip_transaction.h"
#include <cassert>

int main()
{
    using Session = kh3ap::CutsceneSkipTransaction;
    using Action = Session::Action;
    Session state;
    assert(state.advance(0, true, true, true, true, false) == Action::None);
    assert(state.begin(1, 0));
    assert(!state.begin(1, 100)); // repeated start notification does not extend the deadline
    assert(state.advance(100, true, true, true, true, true) == Action::None);
    assert(state.advance(200, true, true, true, true, false) == Action::Request);
    state.mark_requested();
    assert(state.advance(300, true, true, true, true, false) == Action::None);
    assert(state.advance(400, true, false, false, false, false) == Action::Transition);
    assert(!state.begin(1, 500)); // completion cannot redispatch the same occurrence
    assert(state.begin(2, 500));  // replay / chained scene gets a fresh identity
    assert(state.advance(600, true, false, true, true, false) == Action::Cancel);
    assert(state.begin(3, 700));
    assert(state.advance(800, false, true, true, true, false) == Action::Cancel);
    assert(state.begin(4, 900));
    assert(state.advance(15900, true, true, true, true, false) == Action::Cancel);
    assert(state.begin(5, 16000));
    state.mark_requested();
    assert(state.begin(6, 16001)); // re-entrant next scene must not inherit the old requested bit
    assert(!state.requested());
    assert(state.advance(16002, true, true, true, true, false) == Action::Request);
    state.mark_requested();
    assert(state.advance(31001, true, true, true, true, false) == Action::Cancel);
    assert(state.advance(32000, true, true, true, true, false) == Action::None);
    assert(state.begin(7, 40000));
    state.mark_requested();
    state.defer_unissued_request();
    assert(state.advance(40100, true, true, true, true, false) == Action::Request);
    assert(state.advance(55000, true, true, true, true, false) == Action::Cancel);

    // Movie preparation must not consume the single native skip request or
    // extend its deadline. A timed-out preparation never fades the screen.
    assert(state.begin(8, 60000));
    assert(state.advance(60100, true, true, true, true, true) == Action::None);
    assert(!state.requested());
    assert(state.advance(62000, true, true, true, true, false) == Action::Request);
    state.mark_requested();
    assert(state.advance(62100, true, true, true, false, false) == Action::Transition);
    assert(state.begin(9, 70000));
    assert(state.advance(85000, true, true, true, true, true) == Action::Cancel);
    assert(!state.requested());
}
