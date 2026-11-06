#include "kh3ap/features/player/combat/character_style_state.h"
#include <cassert>
#include <limits>

int main()
{
    using namespace kh3ap::character_style;
    const Form spellweaver{153, 1, 1, 829, 1, 0, 17.25f, 30.0f};
    assert(resumable(spellweaver));
    for (float expired : {0.0f, -1.0f, std::numeric_limits<float>::quiet_NaN(),
                          std::numeric_limits<float>::infinity(), 30.1f})
    {
        auto form = spellweaver;
        form.remaining = expired;
        assert(!resumable(form));
    }
    // Situation spells and finishers must never be mistaken for active forms.
    for (int command : {0, 112, 115, 118, 167, 168, 225})
    {
        auto form = spellweaver;
        form.command = command;
        assert(!resumable(form));
    }
    auto form = spellweaver;
    form.weapon_id = 0;
    assert(!resumable(form));
    form = spellweaver;
    form.total = std::numeric_limits<float>::quiet_NaN();
    assert(!resumable(form));
}
