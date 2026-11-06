#include "kh3ap/features/kh02/combat/kh02_battle_level_policy.h"
#include <cassert>
#include <cstdint>
#include <iostream>
#include <limits>
#include <string>

int main()
{
    using kh3ap::kh02_hero_battle_level;
    using kh3ap::kh02_observed_battle_level;
    kh3ap::Kh02EnemyHealthProfile health;
    health.observe(26, 28);
    for (int swap = 0; swap < 100; ++swap)
    {
        assert(health.rescale(15) == 14);
        health.observe(14, 15);
        assert(health.rescale(28) == 26);
        health.observe(26, 28);
    }
    health.rescale(15);
    health.observe(11, 15);
    assert(health.rescale(28) == 21);
    health.observe(0, 28);
    assert(health.rescale(15) == 0);
    health.observe(15, 15);
    assert(health.rescale(28) == 28);
    health.observe(27, 28);
    assert(health.rescale(1) == 1);
    health.observe(1, 1);
    assert(health.rescale(28) == 27);
    kh3ap::Kh02EnemyHealthProfile low_level_health;
    low_level_health.observe(14, 15);
    for (int swap = 0; swap < 100; ++swap)
    {
        assert(low_level_health.rescale(28) == 27);
        low_level_health.observe(27, 28);
        assert(low_level_health.rescale(15) == 14);
        low_level_health.observe(14, 15);
    }
    assert(kh02_hero_battle_level(2, 0, 50, {true, 5}, false) == 55);
    assert(kh02_hero_battle_level(2, 10, {}, {true, 5}, false) == 50);
    assert(kh02_hero_battle_level(2, 10, {}, {false, -10}, false) == 50);
    assert(kh02_hero_battle_level(2, 10, {}, {true, 5}, true) == 50);
    assert(kh02_hero_battle_level(99, 10, {}, {true, 10}, false) == 50);
    assert(kh02_hero_battle_level(2, 0, 50, {true, 5}, true) == 2);
    assert(kh02_hero_battle_level(2, 0, 50, {false, 5}, false) == 2);
    assert(kh02_hero_battle_level(2, 0, 1, {true, -3}, false) == 2);
    assert(kh02_hero_battle_level(2, 0, 99, {true, 10}, false) == 99);
    assert(!kh02_hero_battle_level(2, 0, {}, {true, 0}, false));
    assert(!kh02_hero_battle_level(0, 0, 50, {true, 0}, false));
    assert(!kh02_hero_battle_level(2, 3, 50, {true, 0}, false));
    assert(!kh02_observed_battle_level(0));
    assert(kh02_observed_battle_level(1) == 1);
    assert(kh02_observed_battle_level(kh3ap::kh02_maximum_battle_level) == kh3ap::kh02_maximum_battle_level);
    for (const auto value : {std::optional<int64_t>{}, std::optional<int64_t>{-1},
                             std::optional<int64_t>{kh3ap::kh02_maximum_battle_level + 1},
                             std::optional<int64_t>{std::numeric_limits<int64_t>::max()}})
    {
        bool rejected = false;
        try
        {
            kh02_observed_battle_level(value);
        }
        catch (const std::runtime_error& error)
        {
            rejected = std::string(error.what()) == "kh02_stats_level_signature";
        }
        assert(rejected);
    }
    std::cout << "KH0.2 battle level observation policy passed\n";
}
