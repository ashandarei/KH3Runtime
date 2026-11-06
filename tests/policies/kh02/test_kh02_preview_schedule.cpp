#include "kh3ap/features/kh02/wardrobe/kh02_preview_schedule.h"
#include <cassert>
#include <chrono>

using namespace std::chrono_literals;

int main()
{
    using Schedule = kh3ap::Kh02PreviewSchedule;
    const Schedule::TimePoint started{100s};
    Schedule schedule;

    schedule.material_requested(started);
    assert(!schedule.material_timed_out(started + 10s));
    assert(schedule.material_timed_out(started + 10s + 1ns));

    assert(schedule.begin_capture(started));
    assert(!schedule.begin_capture(started + 65ms));
    assert(schedule.begin_capture(started + 66ms));

    // Rebuilding the render lease permits an immediate first frame without
    // granting another ten seconds to an already pending material request.
    schedule.reset_capture();
    assert(schedule.begin_capture(started + 80ms));
    assert(!schedule.material_timed_out(started + 10s));
    assert(schedule.material_timed_out(started + 11s));

    // A delayed frame performs one capture, with no catch-up burst.
    assert(schedule.begin_capture(started + 30s));
    assert(!schedule.begin_capture(started + 30s));
    assert(!schedule.begin_capture(started + 30s + 65ms));
    assert(schedule.begin_capture(started + 30s + 66ms));

    // A newly accepted load owns a fresh timeout and does not alter cadence.
    schedule.material_requested(started + 31s);
    assert(!schedule.material_timed_out(started + 41s));
    assert(schedule.material_timed_out(started + 41s + 1ns));
    assert(!schedule.begin_capture(started + 30s + 66ms));
}
