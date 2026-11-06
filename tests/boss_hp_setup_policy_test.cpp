#include "kh3ap/features/enemies/boss_hp_setup_policy.h"
#include <cassert>
#include <limits>

int main()
{
    using kh3ap::boss_hp_setup::target_hp;
    assert(target_hp(669, 669, 10) == 603);
    assert(target_hp(334, 669, 10) == 268);
    assert(target_hp(12, 669, 10) == 1);
    assert(target_hp(2, 2, 10) == 1);
    assert(target_hp(100, 100, 10) == 90);
    assert(!target_hp(1, 100, 10));
    assert(!target_hp(0, 100, 10));
    assert(!target_hp(-1, 100, 10));
    assert(!target_hp(101, 100, 10));
    assert(!target_hp(20, -1, 10));
    assert(!target_hp(20, 100, 0));
    assert(!target_hp(20, 100, 25));
    const auto maximum = std::numeric_limits<std::int32_t>::max();
    assert(target_hp(maximum, maximum, 10) == maximum - maximum / 10);
    assert(!target_hp(maximum, static_cast<std::int64_t>(maximum) + 1, 10));
    for (std::int64_t maximum_hp = 2; maximum_hp <= 1'000; ++maximum_hp)
    {
        for (std::int64_t hp = 2; hp <= maximum_hp; ++hp)
        {
            const auto target = target_hp(hp, maximum_hp, 10);
            assert(target && *target >= 1 && *target < hp);
            assert(hp - *target <= std::max<std::int64_t>(1, maximum_hp / 10));
        }
    }
}
