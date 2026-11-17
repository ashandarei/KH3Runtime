#pragma once
#include "kh3ap/features/player/keyblades/style_change_mode.h"

namespace kh3ap
{
    enum class CharacterStyleOwner { Other, Aqua, Kairi };

    constexpr bool character_style_enabled(CharacterStyleOwner owner, bool kh02_story,
                                          RuntimeStyleChangeMode mode, bool shared_equipped,
                                          bool spellweaver_owned, bool seven_wishes_owned)
    {
        if (owner == CharacterStyleOwner::Aqua && kh02_story) { return true; }
        // The general equipped ability overrides per-style seed requirements,
        // just as it does for Sora's Keyblades.
        if (shared_equipped) { return true; }
        if (mode == RuntimeStyleChangeMode::Shared) { return shared_equipped; }
        if (owner == CharacterStyleOwner::Aqua) { return spellweaver_owned; }
        if (owner == CharacterStyleOwner::Kairi) { return seven_wishes_owned; }
        return true;
    }
}
