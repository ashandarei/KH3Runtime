#include "kh3ap/features/player/characters/character_bonus_policy.h"
#include <cassert>

using namespace kh3ap::character_bonuses;

int main()
{
    std::array<uint8_t, record_size> sora{}, alternate{};
    alternate[0] = 3;
    alternate[1] = 16;
    alternate[3] = 1;
    alternate[4] = 1;
    alternate[5] = 3;
    sora[bonus_offset + 3] = 3;
    sora[bonus_offset + 4] = 1;
    sora[bonus_offset + 9] = 1;
    sora[bonus_offset + 16] = 1;
    sora[bonus_offset + 17] = 2;
    sora[bonus_offset + 18] = 3;
    // Equipped IDs, consumable quantities, fixed/unseen flags and magic stay local.
    for (size_t i = 6; i < bonus_offset; ++i) { alternate[i] = 0xa4; }
    alternate[bonus_offset + 24] = 2;
    const auto before = alternate;
    synchronize(alternate, sora);
    assert(alternate[bonus_offset + 3] == 3 && alternate[bonus_offset + 9] == 1);
    for (size_t group = 0; group < 3; ++group)
    {
        const std::array<size_t, 3> capacities{2, 3, 6};
        for (size_t slot = 0; slot < 8; ++slot)
        {
            assert(alternate[0x9c + group * 0x40 + slot * 8] ==
                   (slot < capacities[group] ? 0xa5 : 0xa4));
        }
    }
    for (size_t i = 0; i < record_size; ++i)
    {
        const bool shared = i >= bonus_offset &&
            std::find(shared_kinds.begin(), shared_kinds.end(), i - bonus_offset) != shared_kinds.end();
        const bool availability = i >= 0x9c && i <= 0x154 && (i - 0x9c) % 8 == 0;
        if (!shared && !availability) { assert(alternate[i] == before[i]); }
    }
    const auto once = alternate;
    synchronize(alternate, sora);
    assert(alternate == once);
    // Loading an earlier progression snapshot removes stale derived bonuses.
    sora[bonus_offset + 3] = 0;
    sora[bonus_offset + 16] = 0;
    sora[bonus_offset + 18] = 255;
    synchronize(alternate, sora);
    assert(alternate[bonus_offset + 3] == 0);
    assert((alternate[0xa4] & 1) == 0);
    assert((alternate[0x154] & 1) == 1);
    // Invalid capacities cannot leave a partially updated record.
    alternate[4] = 9;
    const auto invalid = alternate;
    bool rejected = false;
    try { synchronize(alternate, sora); }
    catch (const std::runtime_error&) { rejected = true; }
    assert(rejected && alternate == invalid);
}
