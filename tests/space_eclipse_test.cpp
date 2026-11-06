#include "kh3ap/features/world/space/space_eclipse_policy.h"
#include <algorithm>
#include <cassert>

int main()
{
    using namespace kh3ap::space;
    const std::array<bool, 5> all{true, true, true, true, true};
    for (int stage : {10, 100, 110, 120, 200}) {
        assert(recover_eclipse_unlock(true, true, eclipse_world, all, stage));
        assert(!recover_eclipse_unlock(false, true, eclipse_world, all, stage));
        assert(!recover_eclipse_unlock(true, false, eclipse_world, all, stage));
        assert(!recover_eclipse_unlock(true, true, eclipse_world, all, stage, true));
        assert(!recover_eclipse_unlock(true, true, L"World /Game/Levels/gm/gm_02/gm_02.gm_02", all, stage));
    }
    for (int stage : {-1, 0, 50, 199, 201, 500, 9999})
        assert(!recover_eclipse_unlock(true, true, eclipse_world, all, stage));
    // Every incomplete combination stays locked, including four exterior wins
    // before Colossus. All 120 possible victory orders converge on one unlock.
    for (unsigned mask = 0; mask < 31; ++mask) {
        std::array<bool, 5> wins{};
        for (unsigned i = 0; i < wins.size(); ++i) wins[i] = (mask & (1u << i)) != 0;
        assert(!recover_eclipse_unlock(true, true, eclipse_world, wins, 10));
    }
    std::array<unsigned, 5> order{0, 1, 2, 3, 4};
    do {
        std::array<bool, 5> wins{}; unsigned count{};
        for (auto i : order) {
            wins[i] = true; ++count;
            assert(recover_eclipse_unlock(true, true, eclipse_world, wins, 10) == (count == 5));
        }
        // Reloading/replaying after the native reveal must not reveal again.
        assert(!recover_eclipse_unlock(true, true, eclipse_world, wins, eclipse_boss_ready));
        assert(!recover_eclipse_unlock(true, true, eclipse_world, wins, 9999));
    } while (std::next_permutation(order.begin(), order.end()));
}
