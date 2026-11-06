#include "kh3ap/features/world/space/space_refresh_state.h"
#include <cassert>
#include <chrono>
using namespace std::chrono_literals;
using kh3ap::space::RefreshRequest;

int main()
{
    auto now = RefreshRequest::Clock::time_point{} + 1s;
    RefreshRequest flight, overview;
    int discoveries = 0, menu_refreshes = 0;
    bool flight_ready = false, menu_idle = false;
    auto frame = [&] {
        if (flight.take(now)) {
            if (flight_ready) ++discoveries;
            flight.finish(now, flight_ready);
        }
        if (overview.take(now)) {
            if (menu_idle) ++menu_refreshes;
            overview.finish(now, menu_idle);
        }
        now += 10ms;
    };
    for (int i=0; i<1000; ++i) frame();
    assert(discoveries == 0 && menu_refreshes == 0);

    // Receipts and multiple streamed symbols coalesce while loading. A busy
    // overview can finish later without keeping flight discovery running.
    for (int i=0; i<8; ++i) { flight.request(); overview.request(); }
    for (int i=0; i<500; ++i) frame();
    assert(discoveries == 0 && menu_refreshes == 0);
    flight_ready = true;
    for (int i=0; i<1000; ++i) frame();
    assert(discoveries == 1 && menu_refreshes == 0);
    menu_idle = true;
    for (int i=0; i<1000; ++i) frame();
    assert(discoveries == 1 && menu_refreshes == 1);

    // A later receipt or replacement actor gets one new refresh, then idle.
    flight.request();
    for (int i=0; i<1000; ++i) frame();
    assert(discoveries == 2 && menu_refreshes == 1);

    // Native callbacks during the refresh must not lose a newer event.
    flight.request();
    assert(flight.take(now));
    flight.request();
    flight.finish(now, true);
    frame();
    assert(discoveries == 3);
    for (int i=0; i<1000; ++i) frame();
    assert(discoveries == 3);

    // Leaving a world discards deferred work for that context.
    flight_ready = false;
    flight.request();
    frame();
    flight.clear();
    flight_ready = true;
    for (int i=0; i<1000; ++i) frame();
    assert(discoveries == 3);
}
