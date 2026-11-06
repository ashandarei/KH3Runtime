#include "kh3ap/features/world/space/save_space_policy.h"
#include <cassert>

int main() {
    using namespace std::chrono;
    kh3ap::space::SaveMenuReturn request;
    const auto now=kh3ap::space::SaveMenuReturn::Clock::time_point{}+seconds(20);
    constexpr auto source=L"World /Game/Levels/fz/fz_01/fz_01.fz_01";
    constexpr auto map=L"/Game/Levels/wm/wm_01/wm_01";
    constexpr auto tag=L"wm_01_start01";
    assert(!request.allows(source,map,tag,now));
    request.arm(source,now);
    assert(request.allows(source,map,tag,now));
    // The native fade completion re-enters the routing hook later.
    request.observe(source,now+seconds(1));
    assert(request.allows(source,map,tag,now+seconds(1)));
    assert(!request.allows(L"another world",map,tag,now));
    assert(!request.allows(source,L"/Game/Levels/gm/gm_01/gm_01",tag,now));
    assert(!request.allows(source,map,L"another start",now));
    assert(!request.allows(source,map,tag,now+seconds(10)));
    request.observe(L"World /Game/Levels/wm/wm_01/wm_01.wm_01",now+seconds(2));
    assert(!request.allows(source,map,tag,now+seconds(3)));
    request.arm(source,now);
    request.clear(); // A different jump or rejected request cancels the exception.
    assert(!request.allows(source,map,tag,now));
    request.arm(source,now);
    request.observe(source,now+seconds(10));
    assert(request.source.empty());
}
