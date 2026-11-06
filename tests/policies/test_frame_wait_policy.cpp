#include "kh3ap/hooks/frame_wait/frame_wait_policy.h"
#include <cassert>
#include <limits>
#include <initializer_list>

int main()
{
    using kh3ap::frame_wait_due_time;
    assert(frame_wait_due_time(std::numeric_limits<double>::quiet_NaN())==0);
    assert(frame_wait_due_time(std::numeric_limits<double>::infinity())==0);
    assert(frame_wait_due_time(-1)==0);
    assert(frame_wait_due_time(0)==0);
    assert(frame_wait_due_time(.0005)==0);
    assert(frame_wait_due_time(.0006)==0);
    assert(frame_wait_due_time(.0101)==0);
    for (double remaining: {.0007,.001,.002,.005,.010})
    {
        const auto due=frame_wait_due_time(remaining);
        assert(due<0); // Windows relative timers must use a negative due time.
        const double sleep=-due/10000000.0;
        assert(sleep<=remaining-.0005+1e-12);
        assert(remaining-sleep>=.0005-1e-12);
        assert(remaining-sleep<.0005002);
    }
}
