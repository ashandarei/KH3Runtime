#include "kh3ap/features/kh02/combat/kh02_attack_input_buffer.h"
#include "kh3ap/core/input/options_input_ownership.h"
#include <cassert>

int main()
{
    using Buffer = kh3ap::Kh02AttackInputBuffer;
    Buffer buffer;
    const auto now = Buffer::Clock::now();
    const auto deadline = now + std::chrono::seconds(2);
    // Sora A1 -> Aqua press -> swap back before selection -> Sora press.
    assert(buffer.push(1, false, deadline));
    buffer.attempted(now);
    buffer.handoff();
    assert(buffer.front()->index == 1 && !buffer.front()->attempted);
    assert(buffer.push(1, false, deadline));
    assert(buffer.size() == 2);
    assert(!buffer.push(1, false, deadline));
    buffer.attempted(now);
    buffer.accepted();
    assert(buffer.front()->index == 2 && !buffer.front()->attempted);
    buffer.attempted(now);
    buffer.accepted();
    assert(!buffer.front());
    // An unfulfilled earlier press cannot leave a later hit queued after expiry.
    assert(buffer.push(1, true, deadline));
    assert(buffer.push(1, true, deadline));
    buffer.expire(deadline + std::chrono::milliseconds(1));
    assert(!buffer.front());
    assert(buffer.push(0, false, deadline));
    buffer.clear();
    assert(!buffer.front());
    // Only distinct physical presses enqueue; repeats and releases share ownership.
    kh3ap::RuntimeOptionsInputOwnership ownership;
    int presses = 0;
    auto route = [&](int event)
    {
        return ownership.route(0, L"Gamepad_FaceButton_Bottom", event,
                               [&]
                               {
                                   if (event == 0)
                                   {
                                       ++presses;
                                   }
                                   return true;
                               });
    };
    assert(route(0));
    assert(route(0));
    assert(route(2));
    assert(route(1));
    assert(presses == 1);
    assert(route(0));
    assert(route(1));
    assert(presses == 2);
}
