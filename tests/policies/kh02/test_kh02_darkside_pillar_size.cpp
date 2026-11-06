#include "kh3ap/features/kh02/combat/kh02_darkside_pillar_policy.h"
#include <cassert>
#include <limits>

int main()
{
    using namespace kh3ap;
    const Kh02PillarSizeValues authored{kh02_pillar_authored_size, kh02_pillar_authored_size, kh02_pillar_authored_size,
                                        kh02_pillar_authored_size};
    const auto repaired = kh02_pillar_size_replacement(authored);
    assert(repaired);
    for (const auto& value : *repaired)
    {
        assert(value == kh02_pillar_native_size);
    }
    assert(kh02_pillar_size_replacement(*repaired) == repaired);
    auto unexpected = authored;
    unexpected[0][0] = 9.0f;
    assert(!kh02_pillar_size_replacement(unexpected));
    unexpected = authored;
    unexpected[1] = kh02_pillar_native_size;
    assert(!kh02_pillar_size_replacement(unexpected));
    unexpected = authored;
    unexpected[3][2] = std::numeric_limits<float>::quiet_NaN();
    assert(!kh02_pillar_size_replacement(unexpected));
    unexpected = authored;
    unexpected[2][1] = std::numeric_limits<float>::infinity();
    assert(!kh02_pillar_size_replacement(unexpected));
}
