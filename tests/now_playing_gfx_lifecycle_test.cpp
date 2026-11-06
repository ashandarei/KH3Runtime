#include "kh3ap/features/music/now_playing/now_playing_gfx_lifecycle.h"
#include <array>
#include <cassert>
#include <algorithm>

int main()
{
    using namespace kh3ap::now_playing_gfx;
    // These audited instruction RVAs establish relative offsets independently
    // of the runtime matcher; an incorrect boundary must fail this test.
    static_assert(member_call_offset == 0x3e360ad - 0x3e35fe0);
    static_assert(member_fields_offset == 0x3e2c432 - 0x3e2c360);
    for (std::uint32_t type = 0; type < 256; ++type)
    {
        const auto base = type & 0x8f;
        const bool object_type = base == 8 || base == 9 || base == 10;
        assert(native_value_ready(1, type, 2) == object_type);
        assert(!native_value_ready(0, type, 2));
        assert(!native_value_ready(1, type, 0));
    }
    std::array<std::uint8_t, member_fields_offset + member_fields.size()> body{};
    std::copy(member_fields.begin(), member_fields.end(), body.begin() + member_fields_offset);
    assert(matches(body, member_fields_offset, member_fields));
    assert(!matches(std::span(body).first(body.size() - 1), member_fields_offset, member_fields));
    for (std::size_t index = 0; index < member_fields.size(); ++index)
    {
        body[member_fields_offset + index] ^= 1;
        assert(!matches(body, member_fields_offset, member_fields));
        body[member_fields_offset + index] ^= 1;
    }
    assert(!matches(body, body.size(), member_fields));
    assert(!matches(body, body.size() + 1, member_fields));
}
