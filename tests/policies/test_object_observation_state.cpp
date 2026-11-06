#include "kh3ap/features/player/objects/object_observation_state.h"

#include <cassert>

int main()
{
    using namespace kh3ap;

    RuntimeObjectObservationState state{};
    assert(!state.static_construct_callback_registered());
    assert(!state.should_register_static_construct_callback(false, false));
    assert(!state.should_register_static_construct_callback(true, false));
    assert(!state.should_register_static_construct_callback(false, true));
    assert(state.should_register_static_construct_callback(true, true));

    state.mark_static_construct_callback_registered();
    assert(state.static_construct_callback_registered());
    assert(!state.should_register_static_construct_callback(true, true));

    const auto empty = make_runtime_object_observation_drain_plan(false, 0, 0);
    assert(!empty.report_overflow);
    assert(!empty.dispatch_observations);

    const auto discarded = make_runtime_object_observation_drain_plan(true, 4, 3);
    assert(discarded.report_overflow);
    assert(!discarded.dispatch_observations);

    const auto dispatched = make_runtime_object_observation_drain_plan(false, 4, 0);
    assert(!dispatched.report_overflow);
    assert(dispatched.dispatch_observations);

    const auto dispatched_with_overflow =
            make_runtime_object_observation_drain_plan(false, 4, 3);
    assert(dispatched_with_overflow.report_overflow);
    assert(dispatched_with_overflow.dispatch_observations);

    return 0;
}
