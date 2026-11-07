#include "kh3ap/features/player/characters/character_travel_policy.h"
#include <cassert>

int main()
{
    using kh3ap::character_swap::travel_target_after_swap;
    using kh3ap::character_swap::cinematic_requires_native_sora;
    assert(cinematic_requires_native_sora(L"/Game/Levels/ca/ca_01/ca_01.ca_01"));
    assert(cinematic_requires_native_sora(L"World /Game/Levels/ca/ca_02/ca_02.ca_02"));
    assert(!cinematic_requires_native_sora(L"/Game/Levels/ra/ra_02/ra_02.ra_02"));
    assert(!cinematic_requires_native_sora(L"/Game/Levels/kg_DLC/kg_06/kg_06.kg_06"));
    assert(!cinematic_requires_native_sora(L""));
    int intent = travel_target_after_swap(-1, 16, 0, false);
    assert(intent == 16);
    // A native cinematic Sora handoff must not undo the player's choice.
    intent = travel_target_after_swap(intent, 2, 2, true);
    assert(intent == 16);
    intent = travel_target_after_swap(intent, 16, 2, true);
    assert(intent == 16);
    // An explicit return button does cancel automatic restoration.
    intent = travel_target_after_swap(intent, 2, 2, false);
    assert(intent == -1);
    assert(travel_target_after_swap(-1, 2, 2, true) == -1);
    assert(travel_target_after_swap(16, 17, 0, false) == 17);
    assert(travel_target_after_swap(-1, 10, 3, false) == 10);
}
