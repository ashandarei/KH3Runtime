#include "kh3ap/features/player/keyblades/style_change_mode.h"
#include "kh3ap/features/player/keyblades/keyblade_style_schedule.h"

#include <cassert>
#include <iostream>

using namespace kh3ap;
using namespace std::chrono;

int main()
{
    const auto start = steady_clock::time_point{seconds(100)};
    RuntimeKeybladeStyleSchedule schedule;
    int reads = 0;
    for (int tick = 0; tick < 120; ++tick)
        reads += schedule.take(start + milliseconds(500 * tick), false);
    assert(reads == 12); // 120 old idle reads -> 12 recovery reads per minute.
    assert(schedule.take(start + milliseconds(59600), true)); // weapon / AP grant
    assert(!schedule.take(start + seconds(60), false));
    assert(schedule.take(start + seconds(60), true)); // menu close, even just checked
    assert(schedule.take(start + milliseconds(60500), true)); // deferred pass / failed read
    assert(schedule.take(start + seconds(61), false)); // bounded post-event equipment settling
    assert(!schedule.take(start + milliseconds(61500), false)); // never becomes continuous polling
    assert(!schedule.take(start + seconds(65), false));
    assert(schedule.take(start + seconds(66), false));
    assert(schedule.take(start + seconds(66), true)); // due ascension change bypasses timer
    schedule = {};
    assert(schedule.take(start + seconds(66), false)); // new state bootstraps

    assert(resolve_runtime_style_change_mode(0) == RuntimeStyleChangeMode::Shared);
    assert(resolve_runtime_style_change_mode(1) == RuntimeStyleChangeMode::PerKeyblade);
    assert(resolve_runtime_style_change_mode(std::nullopt) == RuntimeStyleChangeMode::PerKeyblade);
    std::cout << "Keyblade style seed mode and event scheduling tests passed\n";
}
