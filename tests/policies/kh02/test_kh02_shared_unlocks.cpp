#include "kh3ap/features/kh02/rewards/kh02_shared_unlocks.h"
#include "kh3ap/features/kh02/rewards/kh02_reward_notice.h"
#include "kh3ap/features/kh02/rewards/kh02_reward_policy.h"
#include "kh3ap/features/kh02/rewards/kh02_item_slots.h"
#include "kh3ap/features/player/combat/aqua_style_unlock.h"
#include <cassert>
#include <vector>

int main()
{
    using namespace kh3ap;
    aqua_style_unlock::publish(false);
    const auto initial_revision = aqua_style_unlock::revision.load();
    aqua_style_unlock::publish(true);
    assert(aqua_style_unlock::owned());
    assert(aqua_style_unlock::revision.load() != initial_revision);
    const auto owned_revision = aqua_style_unlock::revision.load();
    aqua_style_unlock::publish(true);
    assert(aqua_style_unlock::revision.load() == owned_revision);
    aqua_style_unlock::publish(false);
    assert(!aqua_style_unlock::owned());
    assert(aqua_style_unlock::revision.load() != owned_revision);
    using namespace kh3ap::kh02rewards;
    for (unsigned flags = 0; flags < 256; ++flags)
    {
        const auto editable = editable_item_slot_flags(static_cast<uint8_t>(flags));
        assert((editable & ~item_slot_fixed) == (flags & ~item_slot_fixed));
        assert((flags & item_slot_available) ? !(editable & item_slot_fixed) : editable == flags);
        assert(editable_item_slot_flags(editable) == editable);
    }
    for (unsigned bits = 0; bits < 32; ++bits)
    {
        const SharedUnlockOwnership owned{bool(bits & 1), bool(bits & 2), bool(bits & 4), bool(bits & 8),
                                          bool(bits & 16)};
        for (const bool shared_style : {false, true})
        {
            const auto plan = shared_unlock_plan(owned, shared_style);
            assert(*plan[2].enabled == (owned.magic || owned.links));
            for (const uint16_t initial : {uint16_t{0}, uint16_t{0xffff}, uint16_t{0xa53a}})
            {
                auto flags = initial;
                uint16_t controlled_mask{};
                for (const auto& effect : plan)
                {
                    if (effect.enabled)
                    {
                        controlled_mask |= effect.mask;
                    }
                }
                const auto setter = [&](size_t index, bool enabled)
                {
                    assert(index < 4); // Per-pawn restriction setters 6/7 are never admitted.
                    const auto mask = plan[index].mask;
                    flags = enabled ? flags | mask : flags & ~mask;
                    return true;
                };
                assert(apply_shared_unlock_plan(initial, plan, setter));
                assert(shared_unlocks_match(flags, plan));
                assert(((flags ^ initial) & ~controlled_mask) == 0);
                assert(shared_style || ((flags ^ initial) & style_change_mask) == 0);
                unsigned calls{};
                assert(apply_shared_unlock_plan(flags, plan,
                                                [&](size_t, bool)
                                                {
                                                    ++calls;
                                                    return true;
                                                }));
                assert(calls == 0);
            }
        }
    }
    const auto enabled = shared_unlock_plan({true, true, true, true, true}, true);
    unsigned failed_calls{};
    assert(!apply_shared_unlock_plan(0, enabled,
                                     [&](size_t, bool)
                                     {
                                         ++failed_calls;
                                         return false;
                                     }));
    assert(failed_calls == 1);
    assert(uses_native_ability_popup(L"controller_unlock"));
    assert(uses_native_ability_popup(L"durable_sora_ability"));
    assert(!uses_native_ability_popup(L"bp_tres_get_item"));
    assert(!uses_native_ability_popup(L"durable_sora_vbonus"));
    assert(reward_notice(L"controller_unlock", L"Unlock: Magic") == L"Received Unlock: Magic");
    assert(reward_notice(L"controller_unlock", L"Unlock: Links") == L"Received Unlock: Links");
    assert(reward_notice(L"bp_tres_get_item", L"Potion") == L"Received Potion for Sora");
    assert(reward_notice(L"durable_sora_vbonus", L"Magic: Fire") == L"Received Magic: Fire for Sora");
}
