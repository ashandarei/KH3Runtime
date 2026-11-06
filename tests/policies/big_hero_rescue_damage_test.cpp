#include "kh3ap/features/story/world_fixes/big_hero_rescue_damage_policy.h"
#include <cassert>
#include <string>

int main()
{
    using namespace kh3ap;
    for (const auto actor : big_hero_rescue_damage_classes)
    {
        assert(block_big_hero_rescue_damage(L"TresGimmickHitCount", actor));
        // Matching magic must still reach the native per-leg attribute filter
        // and its forced-destruction callback, even after the normal counter
        // has stopped receiving hits.
        assert(!block_big_hero_rescue_damage(L"TresGimmickHitCountMagic", actor));
        assert(!block_big_hero_rescue_damage(L"TresGimmickHitCountDaruma", actor));
        assert(!block_big_hero_rescue_damage(L"", actor));
    }
    for (const auto suffix : {L"base", L"01", L"02", L"parts_base", L"06"})
    {
        const auto stem = std::wstring(L"g_bx_darkcube_") + suffix;
        const auto path = std::wstring(L"/Game/Blueprints/Gimmick/bx/g_bx_darkcube/") + stem + L"." + stem + L"_C";
        assert(!block_big_hero_rescue_damage(L"TresGimmickHitCount", path));
    }
    // A similarly named class in another package is not a rescue actor.
    assert(!block_big_hero_rescue_damage(L"TresGimmickHitCount", L"/Game/Mods/g_bx_darkcube_04.g_bx_darkcube_04_C"));
    assert(!block_big_hero_rescue_damage(L"TresGimmickHitCount", L""));
}
