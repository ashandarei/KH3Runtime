#include "kh3ap/features/treasure/music_chest/music_pause_policy.h"
#include <cassert>
int main() {
    using namespace std::chrono;
    kh3ap::RuntimeMusicPausePolicy p;
    auto now=kh3ap::RuntimeMusicPausePolicy::Clock::now();
    assert(!p.should_release(true,now));
    p.begin(now);
    assert(!p.should_release(true,now+seconds(1)));
    assert(!p.should_release(false,now+seconds(2)));
    assert(!p.should_release(false,now+minutes(10)));
    assert(p.should_release(true,now+minutes(10)));
    p.finish();assert(!p.should_release(true,now+minutes(11)));
    p.begin(now);assert(p.should_release(true,now+seconds(30)));
}
