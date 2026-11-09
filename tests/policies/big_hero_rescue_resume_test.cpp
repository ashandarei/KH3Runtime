#include "kh3ap/features/story/world_fixes/big_hero_rescue_resume.h"
#include <cassert>
#include <vector>

using namespace kh3ap;

int main()
{
    constexpr uint64_t library = 0x123456789ABC;
    constexpr uint64_t setter = 0x23456789ABCD;
    const std::array<HeroRescueScriptName, 4> flags{{{1, 1, 0}, {2, 2, 0}, {3, 3, 0}, {4, 4, 0}}};
    std::vector<uint8_t> original(0xA6175, 0x53);
    const auto put = [&](size_t at, const auto& value) {
        std::memcpy(original.data() + at, &value, sizeof(value));
    };
    for (size_t index = 0; index < 4; ++index)
    {
        const size_t at = 0x20275 + index * 0x2E;
        original[at] = 0x19;
        original[at + 1] = 0x20;
        put(at + 2, library);
        put(at + 0xA, uint32_t{24});
        put(at + 0xE, uint64_t{0});
        original[at + 0x16] = 0x1C;
        put(at + 0x17, setter);
        original[at + 0x1F] = 0x21;
        put(at + 0x20, flags[index]);
        original[at + 0x2C] = 0x27;
        original[at + 0x2D] = 0x16;
    }
    original[0x2032D] = 0x4D;
    auto patched = original;
    assert(patch_big_hero_rescue_resume(patched, library, setter, flags) == HeroRescueResumeResult::Patched);
    assert(patch_big_hero_rescue_resume(patched, library, setter, flags) == HeroRescueResumeResult::AlreadyPatched);
    for (size_t index = 0; index < original.size(); ++index)
    {
        if (index < 0x20275 || index >= 0x2027A)
        {
            assert(patched[index] == original[index]);
        }
    }
    // Execute the repaired restore tail for every combination of rescued
    // heroes. None may be added, including zero, one, three, and all four.
    for (unsigned mask = 0; mask < 16; ++mask)
    {
        unsigned restored = mask;
        size_t pc = 0x20275;
        while (patched[pc] != 0x4D)
        {
            if (patched[pc] == 0x06)
            {
                uint32_t target{};
                std::memcpy(&target, patched.data() + pc + 1, sizeof(target));
                pc = target;
            }
            else
            {
                assert(patched[pc] == 0x19);
                restored |= 1u << ((pc - 0x20275) / 0x2E);
                pc += 0x2E;
            }
        }
        assert(restored == mask);
        assert((restored == 15) == (mask == 15));
    }
    // Unknown/revised assets and partially corrupted patches fail atomically.
    for (const size_t at : {size_t{0x20275}, size_t{0x202A3 + 0x17},
                           size_t{0x202D1 + 0x20}, size_t{0x202FF + 0x2C}, size_t{0x2032D}})
    {
        auto changed = original;
        changed[at] ^= 0x80;
        const auto before = changed;
        assert(patch_big_hero_rescue_resume(changed, library, setter, flags) == HeroRescueResumeResult::Rejected);
        assert(changed == before);
    }
    auto changed = original;
    changed.pop_back();
    assert(patch_big_hero_rescue_resume(changed, library, setter, flags) == HeroRescueResumeResult::Rejected);
    assert(patch_big_hero_rescue_resume(original, 0, setter, flags) == HeroRescueResumeResult::Rejected);
    patched[0x202D1 + 0x20] ^= 1;
    const auto before = patched;
    assert(patch_big_hero_rescue_resume(patched, library, setter, flags) == HeroRescueResumeResult::Rejected);
    assert(patched == before);
}
