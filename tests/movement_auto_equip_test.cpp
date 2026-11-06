#include "kh3ap/features/grants/abilities/movement_auto_equip_policy.h"
#include <cassert>
#include <string>

int main()
{
    using namespace kh3ap;
    for (const auto token : {L"GLIDE", L"AIRSLIDE", L"HIGHJUMP", L"DOUBLEFLIGHT",
                            L"SUPERJUMP", L"SUPERSLIDE", L"DODGE", L"POLE_SPIN", L"POLE_SWING"})
    {
        assert(is_auto_equip_movement_ability(std::wstring(L"ETresAbilityKind::") + token));
    }
    assert(!is_auto_equip_movement_ability(L"ETresAbilityKind::AIRDODGE"));
    assert(!is_auto_equip_movement_ability(L"ETresAbilityKind::COMBO_PLUS"));
    assert(!is_auto_equip_movement_ability(L"ETresAbilityKind::AP_WALL_RUN"));
    assert(movement_auto_equip_mask(0, 10, 9, 1, 3) == 0x2); // exact AP
    assert(movement_auto_equip_mask(0, 10, 10, 1, 3) == 0); // insufficient
    assert(movement_auto_equip_mask(0, 10, 10, 0, 3) == 0x2); // zero cost
    assert(movement_auto_equip_mask(0xb, 10, 1, 1, 3) == 0x20); // second copy
    assert(movement_auto_equip_mask(0xbb, 10, 2, 1, 3) == 0x200); // third copy
    assert(movement_auto_equip_mask(0xbbb, 10, 3, 1, 5) == 0); // saturated
    assert(movement_auto_equip_mask(0xb, 10, 1, 1, 1) == 0); // table max
    assert(movement_auto_equip_mask(0, 0, 1, 0, 3) == 0); // already over budget
    assert(movement_auto_equip_mask(0, -1, 0, 0, 3) == 0);
    assert(movement_auto_equip_mask(0, 10, -1, 1, 3) == 0);
    // A manually unequipped first copy stays off; only the new copy is selected.
    assert(movement_auto_equip_mask(0x9, 10, 0, 1, 3) == 0x20);
    assert(equipped_ability_copies(0xb) == 1);
    assert(equipped_ability_copies(0xb9) == 1);
    assert(equipped_ability_copies(0xbbb | (2u << 18)) == 5);
    assert(movement_auto_equip_mask(1u << 18, 10, 1, 1, 1) == 0);
}
