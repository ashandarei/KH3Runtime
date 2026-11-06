#include "kh3ap/features/tutorials/world_keyblade_bypass.h"

#include <cassert>
#include <cstring>
#include <vector>

namespace
{
    // Actual ra_01 EGS statement captured with passive ReadProcessMemory.
    // Retain its layout and pointer identities, without executing any asset.
    constexpr unsigned char statement[] = {
        0x19,0x20,0x70,0xc7,0x82,0xa2,0x38,0x02,0x00,0x00,0x0d,0x00,0x00,0x00,
        0x20,0xf4,0x63,0x7f,0x39,0x02,0x00,0x00,
        0x1c,0x00,0x43,0xaf,0xa1,0x38,0x02,0x00,0x00,0x17,0x24,0x05,0x16,
        0x19,0x20,0x70,0xc7,0x82,0xa2,0x38,0x02,0x00,0x00,0x3a,0x00,0x00,0x00,
        0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
        0x1c,0x60,0xf1,0xb4,0xa1,0x38,0x02,0x00,0x00,0x17,
        0x2f,0xd0,0x79,0x47,0xa1,0x38,0x02,0x00,0x00,0x20,0x00,0x00,0x00,
        0x5b,0x40,0xe1,0x02,0x00,0x1d,0x19,0xc4,0xcf,0x7b,
        0x21,0x9b,0x44,0x03,0x00,0x9b,0x44,0x03,0x00,0x00,0x00,0x00,0x00,
        0x17,0x30,0x00,0x20,0xf4,0x63,0x7f,0x39,0x02,0x00,0x00,0x16,0x4d,
        0x14,0x00,0xa0,0x24,0xb2,0x81,0x38,0x02,0x00,0x00
    };
    constexpr size_t at = 0x80;
    constexpr uint32_t resume = at + 0x51;
    auto fixture() -> std::vector<uint8_t>
    {
        std::vector<uint8_t> bytes(0x140, 0xCC);
        std::memcpy(bytes.data() + at - 0x23, statement, sizeof(statement));
        std::memcpy(bytes.data() + at + 0x2E, &resume, sizeof(resume));
        return bytes;
    }
    auto patch(std::vector<uint8_t>& bytes, uint8_t weapon = 5,
        uint64_t conversion = 0x238A1AF4300, uint64_t grant = 0x238A1B4F160)
    {
        return kh3ap::try_patch_runtime_world_keyblade_bytecode(
            bytes.data(), bytes.size(), weapon, conversion, grant, 0x3449B);
    }
}
int main()
{
    using Result = kh3ap::RuntimeWorldKeybladeBypassResult;
    auto bytes = fixture();
    const auto before = bytes;
    assert(patch(bytes) == Result::Patched);
    assert(bytes[at] == 0x06);
    uint32_t target{}; std::memcpy(&target, bytes.data() + at + 1, sizeof(target));
    assert(target == resume);
    for (size_t i = 0; i < bytes.size(); ++i)
        assert((i >= at && i < at + 5) || bytes[i] == before[i]);
    const auto once = bytes;
    assert(patch(bytes) == Result::AlreadyPatched && bytes == once);
    // Arendelle resumes an earlier shared ending block, rather than the next
    // physical statement after the grant. Follow the original latent linkage.
    bytes = fixture();
    const uint32_t backward_resume = 0x20;
    bytes[backward_resume] = 0x19;
    std::memcpy(bytes.data() + at + 0x2E, &backward_resume, sizeof(backward_resume));
    assert(patch(bytes) == Result::Patched);
    std::memcpy(&target, bytes.data() + at + 1, sizeof(target));
    assert(target == backward_resume);
    assert(patch(bytes) == Result::AlreadyPatched);
    // Other weapons and shared AP grant functions must never be redirected.
    for (int mismatch = 0; mismatch < 3; ++mismatch)
    {
        bytes = fixture();
        assert(patch(bytes, mismatch == 0 ? 1 : 5,
            mismatch == 1 ? 0x123 : 0x238A1AF4300,
            mismatch == 2 ? 0x456 : 0x238A1B4F160) ==
            (mismatch == 0 ? Result::Rejected : Result::NoMatchingGrant));
        assert(bytes == before);
    }
    bytes = fixture();
    assert(patch(bytes, 6) == Result::NoMatchingGrant && bytes == before);
    // Invalid latent resume, callback, variable and truncated code fail closed.
    for (size_t offset : {at + 0x2E, at + 0x38, at + 0x47, at - 0x19, at + 0x50})
    {
        bytes = fixture(); bytes[offset] ^= 1;
        const auto bad = bytes;
        assert(patch(bytes) == Result::Rejected && bytes == bad);
    }
    bytes = fixture(); bytes.resize(at + 0x50);
    const auto truncated = bytes;
    assert(patch(bytes) == Result::Rejected && bytes == truncated);
    // A second malformed candidate prevents any earlier valid write.
    bytes = fixture(); bytes.resize(0x280, 0xCC);
    std::memcpy(bytes.data() + 0x180 - 0x23, statement, sizeof(statement));
    const auto two = bytes;
    assert(patch(bytes) == Result::Rejected && bytes == two);
}
