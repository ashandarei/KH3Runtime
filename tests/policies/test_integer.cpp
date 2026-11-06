#include "kh3ap/core/integer.h"
#include <cassert>

int main()
{
    using kh3ap::checked_native_integer;
    assert(checked_native_integer(0) == 0);
    assert(checked_native_integer(std::numeric_limits<int32_t>::min()).has_value());
    assert(checked_native_integer(std::numeric_limits<int32_t>::max()).has_value());
    assert(!checked_native_integer(static_cast<int64_t>(std::numeric_limits<int32_t>::max()) + 1));
    assert(!checked_native_integer(static_cast<int64_t>(std::numeric_limits<int32_t>::min()) - 1));
    assert(!checked_native_integer((int64_t{1} << 32) + 640));
    assert(!checked_native_integer(std::numeric_limits<int64_t>::max()));
    assert(!checked_native_integer(std::numeric_limits<int64_t>::min()));
    assert(!checked_native_integer((int64_t{1} << 32) + 1));
    assert(!checked_native_integer((int64_t{1} << 32) - 1));
    assert(checked_native_integer(-1) == -1);
    assert(checked_native_integer(1) == 1);
    static_assert(checked_native_integer(42) == 42);
}
