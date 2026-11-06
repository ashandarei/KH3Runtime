#include "kh3ap/core/platform/mom_focus_handoff.h"
#include <cassert>
#include <iostream>

int main() {
    using Handoff = kh3ap::RuntimeMoMFocusHandoff;
    using Target = Handoff::Target;
    using namespace std::chrono_literals;
    auto now = Handoff::Clock::time_point{};
    Handoff handoff;
    assert(handoff.observe(0, false, false, true, false, now) == Target::none);
    // Process comes first, then a usable game window; console/splash is ignored.
    assert(handoff.observe(10, false, false, true, false, now) == Target::none);
    assert(handoff.observe(10, true, false, true, false, now + 2s) == Target::mom);
    assert(handoff.observe(10, true, false, true, false, now + 3s) == Target::mom);
    assert(handoff.observe(10, true, true, true, false, now + 4s) == Target::none);
    assert(handoff.observe(10, true, false, true, false, now + 5s) == Target::none); // deliberate Alt-Tab
    // Actual exit, not a song result or a missing/minimized window, returns KH3.
    assert(handoff.observe(10, false, false, true, false, now + 6s) == Target::none);
    assert(handoff.observe(0, false, false, true, false, now + 7s) == Target::kh3);
    assert(handoff.observe(0, false, false, true, true, now + 8s) == Target::none);
    assert(handoff.observe(0, false, false, true, false, now + 9s) == Target::none);
    // A new process supersedes a pending return and gets one new handoff.
    assert(handoff.observe(11, true, false, true, false, now + 10s) == Target::mom);
    assert(handoff.observe(0, false, false, false, false, now + 11s) == Target::none);
    assert(handoff.observe(12, true, false, true, false, now + 12s) == Target::mom);
    assert(handoff.observe(13, true, false, true, false, now + 13s) == Target::mom);
    assert(handoff.observe(13, true, false, true, false, now + 74s) == Target::none);
    assert(handoff.observe(13, true, false, true, false, now + 75s) == Target::none);
    assert(handoff.observe(0, false, false, true, false, now + 76s) == Target::kh3);
    assert(handoff.observe(0, false, false, true, false, now + 87s) == Target::none);
    std::cout << "MoM focus handoff tests passed\n";
}
