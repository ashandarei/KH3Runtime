#include "kh3ap/features/story/world_fixes/lump_of_horror_retry.h"
#include <cassert>
#include <vector>

int main()
{
    constexpr uintptr_t lump = 0x12345678, method = 0x87654321;
    constexpr uintptr_t pawn_local = 0x10203040, success_local = 0x50607080;
    std::vector<uint8_t> original(304545, 0);
    const auto put = [&](size_t i, const auto& value) {
        std::memcpy(original.data() + i, &value, sizeof(value));
    };
    original[304357] = 0x2e;
    put(304358, lump);
    put(304349, pawn_local);
    put(304385, success_local);
    original[304393] = 0x38;
    original[304394] = 0x47;
    put(304396, pawn_local);
    original[304404] = 0x4f;
    put(304406, success_local);
    original[304414] = 0x19;
    put(304416, pawn_local);
    put(304424, uint32_t{10});
    original[304436] = 0x1c;
    put(304437, method);
    original[304445] = 0x16;
    original[304446] = 0x06;
    put(304447, uint32_t{292842});

    using kh3ap::LumpRetryPatch;
    auto script = original;
    assert(kh3ap::patch_lump_retry_skip(script, lump, method) == LumpRetryPatch::applied);
    auto expected = original;
    std::fill(expected.begin() + 304404, expected.begin() + 304414, 0x0b);
    expected[304414] = 0x1a;
    assert(script == expected);
    assert(kh3ap::patch_lump_retry_skip(script, lump, method) == LumpRetryPatch::already_applied);
    assert(script == expected);

    for (size_t offset : {304358, 304396, 304406, 304416, 304424, 304437, 304447})
    {
        auto different = original;
        different[offset] ^= 1;
        const auto before = different;
        assert(kh3ap::patch_lump_retry_skip(different, lump, method) == LumpRetryPatch::rejected);
        assert(different == before);
    }
    assert(kh3ap::patch_lump_retry_skip({}, lump, method) == LumpRetryPatch::rejected);
}
