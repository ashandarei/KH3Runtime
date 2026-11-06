#include "kh3ap/features/kh02/story/kh02_loading_policy.h"
#include <cassert>
#include <vector>

int main()
{
    constexpr size_t site = 30506;
    constexpr uintptr_t kismet = 0x12345678, delay = 0x76543210;
    std::vector<uint8_t> code(site + 76);
    auto* p = code.data() + site;
    auto put = [&]<class T>(size_t offset, T value) { std::memcpy(p + offset, &value, sizeof(value)); };
    p[0] = 0x19;
    p[1] = 0x20;
    put(2, kismet);
    put(10, uint32_t{54});
    p[22] = 0x1c;
    put(23, delay);
    p[31] = 0x17;
    p[32] = 0x1e;
    put(33, 3.f);
    p[37] = 0x2f;
    put(46, uint32_t{32});
    p[50] = 0x5b;
    put(51, int32_t{7860});
    p[55] = 0x1d;
    put(56, int32_t{279349679});
    p[60] = 0x21;
    p[73] = 0x17;
    p[74] = 0x30;
    p[75] = 0x16;
    const auto original = code;
    assert(kh3ap::kh02_castle_revisit_delay(code, kismet, delay) == site + 33);
    assert(code == original);
    assert(!kh3ap::kh02_castle_revisit_delay({code.data(), code.size() - 1}, kismet, delay));
    assert(!kh3ap::kh02_castle_revisit_delay(code, 0, delay));
    assert(!kh3ap::kh02_castle_revisit_delay(code, kismet, delay + 1));
    try
    {
        kh3ap::Kh02CastleDelayOverride override;
        assert(override.apply(code, kismet, delay));
        float duration = -1.f;
        std::memcpy(&duration, p + 33, sizeof(duration));
        assert(duration == 0.f);
        assert(!override.apply(code, kismet, delay));
        kh3ap::Kh02CastleDelayOverride nested;
        assert(!nested.apply(code, kismet, delay));
        throw 42;
    }
    catch (int value)
    {
        assert(value == 42);
    }
    assert(code == original);
    {
        kh3ap::Kh02CastleDelayOverride override;
        assert(!override.apply(code, 0, delay));
    }
    assert(code == original);
    for (size_t offset :
         {size_t{0}, size_t{10}, size_t{14}, size_t{31}, size_t{33}, size_t{50}, size_t{51}, size_t{56}, size_t{73}})
    {
        p[offset] ^= 0x80;
        assert(!kh3ap::kh02_castle_revisit_delay(code, kismet, delay));
        p[offset] ^= 0x80;
    }
    put(33, 0.f);
    assert(!kh3ap::kh02_castle_revisit_delay(code, kismet, delay));
}
