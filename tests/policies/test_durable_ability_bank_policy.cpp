#include "kh3ap/features/grants/abilities/durable_ability_bank_policy.h"

#include <cassert>
#include <cstdint>
#include <initializer_list>

int main()
{
    using namespace kh3ap;

    // Live EGS Air Slide receipt #2: one identical durable copy, but the
    // player has equipped and viewed only the active bank's first slot.
    assert(runtime_durable_ability_banks_match(0x044b, 0x044d));

    // The same distinction applies to the second and third slots, including
    // future grants after equipping earlier copies in camp.
    for (uint32_t flags = 0; flags <= runtime_ability_menu_flags; ++flags)
    {
        if ((flags & ~runtime_ability_menu_flags) != 0)
        {
            continue;
        }
        for (const uint32_t ownership : {0x0000u, 0x0009u, 0x0099u, 0x0999u})
        {
            assert(runtime_durable_ability_banks_match(ownership | flags, ownership));
            assert(runtime_durable_ability_banks_match(ownership, ownership | flags));
        }
    }

    // A copy/ownership/count mismatch must still block delivery. Unknown
    // bits remain fail-closed too; this is not a blanket bank reconciliation.
    for (unsigned bit = 0; bit < 32; ++bit)
    {
        const uint32_t difference = uint32_t{1} << bit;
        const bool menu_only = (difference & runtime_ability_menu_flags) != 0;
        assert(runtime_durable_ability_banks_match(0x044b, 0x044b ^ difference) == menu_only);
    }

    // Normalizing the new second/third ownership slot must still leave
    // matching grant state while retaining each bank's own menu choices.
    assert(runtime_durable_ability_banks_match(0x04db, 0x04dd));
    assert(runtime_durable_ability_banks_match(0x0ddb, 0x0ddd));
    assert(!runtime_durable_ability_banks_match(0x04db, 0x044d));
    assert(!runtime_durable_ability_banks_match(0x0ddb, 0x04dd));
}
