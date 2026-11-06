#include "kh3ap/features/kh02/runtime/kh02_recovery_state.h"
#include <cassert>

int main()
{
    using State = kh3ap::Kh02RecoveryState;
    const auto start = State::Clock::time_point{};

    // An active failure queues recovery once. Duplicate observations and
    // diagnostic requests cannot extend the original bounded attempt.
    State recovery;
    assert(recovery.begin(start));
    assert(!recovery.begin(start + std::chrono::seconds(10)));
    recovery.request_return(start + std::chrono::seconds(10));
    assert(recovery.poll_return(start + std::chrono::seconds(14)) == State::Action::attempt);
    assert(recovery.poll_return(start + State::return_timeout) == State::Action::timed_out);
    assert(!recovery.return_pending());
    assert(recovery.poll_return(start + std::chrono::seconds(16)) == State::Action::none);
    assert(recovery.phase() == State::Phase::failed);

    // An explicit retry is possible after the safe-return window expires.
    // A dispatched travel cannot be duplicated while arrival is pending.
    const auto retry = start + std::chrono::seconds(20);
    recovery.request_return(retry);
    assert(recovery.poll_return(retry) == State::Action::attempt);
    recovery.return_dispatched(retry);
    recovery.request_return(retry + std::chrono::seconds(1));
    assert(!recovery.return_pending());
    assert(recovery.notification_pending());
    assert(recovery.phase() == State::Phase::exiting);
    assert(recovery.poll_notification(retry, false) == State::Action::none);

    // The first playable GoA observation claims the notice before native UI
    // dispatch, so even a throwing or unavailable UI cannot show it twice.
    assert(recovery.poll_notification(retry + std::chrono::seconds(2), true) == State::Action::attempt);
    assert(recovery.notification_attempted());
    assert(!recovery.was_notification_shown());
    assert(recovery.phase() == State::Phase::returned);
    assert(recovery.poll_notification(retry + std::chrono::seconds(3), true) == State::Action::none);
    recovery.notification_shown();
    assert(recovery.was_notification_shown());
    assert(!recovery.begin(retry + std::chrono::seconds(4)));

    // A missing/paused/loading GoA cannot keep a notification alive forever.
    State unavailable;
    unavailable.begin(start);
    unavailable.return_dispatched(start);
    assert(unavailable.poll_notification(start + State::notification_timeout, false) == State::Action::timed_out);
    assert(!unavailable.notification_pending());
    assert(!unavailable.notification_attempted());
    assert(unavailable.poll_notification(start + State::notification_timeout + std::chrono::seconds(1), true) ==
           State::Action::none);

    // A native travel exception leaves the session failed and no notice queued.
    State travel_failed;
    travel_failed.begin(start);
    travel_failed.return_failed();
    assert(!travel_failed.return_pending());
    assert(!travel_failed.notification_pending());
    assert(travel_failed.phase() == State::Phase::failed);
}
