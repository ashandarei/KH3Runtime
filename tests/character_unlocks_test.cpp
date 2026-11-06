#include "kh3ap/features/player/characters/character_unlocks.h"
#include <cassert>

int main()
{
    using namespace kh3ap::character_unlocks;
    received_mask.store(0);
    assert(available(-1));
    for (int player : {8, 10, 16, 17})
    {
        assert(!available(player));
    }
    assert(token_bit(L"KH3AP_CHARACTER_UNLOCK::8") == riku_unlock_mask);
    assert(token_bit(L"KH3AP_CHARACTER_UNLOCK::10::1") == 0);
    assert(token_bit(L"KH3AP_PARTY_MEMBER_UNLOCK::PartyMember_015") == 0);
    received_mask.store(token_bit(L"KH3AP_CHARACTER_UNLOCK::16"));
    assert(available(16));
    assert(!available(17));
    assert(!available(10));
    received_mask.fetch_or(token_bit(L"KH3AP_CHARACTER_UNLOCK::17"));
    received_mask.fetch_or(token_bit(L"KH3AP_CHARACTER_UNLOCK::10"));
    assert(available(16) && available(17) && available(10));
    assert(!available(8));
    received_mask.fetch_or(token_bit(L"KH3AP_CHARACTER_UNLOCK::8"));
    assert(available(8) && available(16) && available(17) && available(10));
    // A different seed starts locked, regardless of a previous seed's receipts.
    received_mask.store(0);
    assert(!available(8) && !available(16) && !available(17) && !available(10));
}
