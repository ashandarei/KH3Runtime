#include "kh3ap/features/player/combat/character_style_unlock_policy.h"
#include <cassert>
#include <initializer_list>

int main()
{
    using namespace kh3ap;
    for (int bits = 0; bits < 8; ++bits)
    {
        const bool shared = bits & 1;
        const bool spellweaver = bits & 2;
        const bool seven_wishes = bits & 4;
        for (const auto owner : {CharacterStyleOwner::Aqua, CharacterStyleOwner::Kairi})
        {
            assert(character_style_enabled(owner, false, RuntimeStyleChangeMode::Shared,
                                           shared, spellweaver, seven_wishes) == shared);
            assert(character_style_enabled(owner, false, RuntimeStyleChangeMode::PerKeyblade,
                                           shared, spellweaver, seven_wishes) ==
                   (owner == CharacterStyleOwner::Aqua ? spellweaver : seven_wishes));
        }
        for (const auto mode : {RuntimeStyleChangeMode::Shared, RuntimeStyleChangeMode::PerKeyblade})
        {
            assert(character_style_enabled(CharacterStyleOwner::Aqua, true, mode,
                                           shared, spellweaver, seven_wishes));
        }
    }
}
