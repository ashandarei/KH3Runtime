#include "kh3ap/features/kh02/runtime/kh02_lifecycle.h"
#include <cassert>

int main()
{
    using namespace kh3ap;
    using Phase = Kh02Lifecycle::Phase;
    Kh02Lifecycle state;
    state.observe(Kh02WorldKind::other, true);
    assert(state.phase() == Phase::dormant);
    state.observe(Kh02WorldKind::portal, true);
    assert(state.phase() == Phase::dormant);
    state.prepare();
    state.observe(Kh02WorldKind::portal, false);
    assert(state.phase() == Phase::preparing);
    state.observe(Kh02WorldKind::unavailable, true);
    assert(state.phase() == Phase::preparing);
    state.observe(Kh02WorldKind::story, true);
    assert(state.phase() == Phase::active);
    // Same-world Retry and another imported map must not retire the runtime.
    state.observe(Kh02WorldKind::story, false);
    state.prepare();
    assert(state.phase() == Phase::active);
    state.observe(Kh02WorldKind::story, true);
    assert(state.phase() == Phase::active);
    state.observe(Kh02WorldKind::unavailable, false);
    assert(state.phase() == Phase::active);
    state.observe(Kh02WorldKind::other, true);
    assert(state.phase() == Phase::leaving);
    state.finish_leaving();
    state.observe(Kh02WorldKind::other, false);
    assert(state.phase() == Phase::dormant);
    state.finish_leaving();
    assert(state.phase() == Phase::dormant);
    // Cancelled pre-entry travel still needs one cleanup when its origin departs.
    state.prepare();
    state.observe(Kh02WorldKind::other, true);
    assert(state.phase() == Phase::leaving);
    state.finish_leaving();
    // A save loaded directly into an imported world activates without a portal.
    state.observe(Kh02WorldKind::story, true);
    assert(state.phase() == Phase::active);
    assert(state.phase_name() == "active");
    state.cancel_preparation();
    assert(state.phase() == Phase::active);
    Kh02Lifecycle cancelled;
    cancelled.prepare();
    cancelled.cancel_preparation();
    assert(cancelled.phase() == Phase::leaving);
    cancelled.observe(Kh02WorldKind::portal, false);
    cancelled.finish_leaving();
    assert(cancelled.phase() == Phase::dormant);
}
