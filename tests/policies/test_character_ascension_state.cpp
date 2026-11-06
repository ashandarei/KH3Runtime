#include "kh3ap/features/player/characters/character_ascension_state.h"
#include <cassert>
#include <initializer_list>

int main()
{
    using kh3ap::character_swap::CharacterAscensionState;
    using kh3ap::character_swap::is_ascension_cinematic;
    constexpr auto scene = L"/Game/Levels/bt/bt_04/umap/bt_04_camera_assension."
                           L"bt_04_camera_assension:PersistentLevel.assention";
    assert(is_ascension_cinematic(L"/Game/Levels/bt/bt_04/bt_04.bt_04", scene));
    assert(!is_ascension_cinematic(L"/Game/Levels/mi/mi_03/mi_03.mi_03", scene));
    assert(!is_ascension_cinematic(L"/Game/Levels/bt/bt_04/bt_04.bt_04", L"assention"));
    assert(!is_ascension_cinematic(L"/Game/Levels/bt/bt_04/bt_04.bt_04",
                                 L"/Game/Cutscene/another_movie"));
    for (const int character : {8, 10, 16, 17})
    {
        CharacterAscensionState state;
        state.start(character);
        assert(state.active && !state.restore_pending);
        assert(state.allows_target(0, 0));
        for (const int alternate : {8, 9, 10, 16, 17})
        {
            assert(!state.allows_target(alternate, 0));
        }
        // Duplicate entry after the forced Sora handoff retains the choice.
        state.start(-1);
        assert(state.restore_character == character);
        state.finish();
        assert(!state.active && state.restore_pending);
        assert(state.allows_target(character, 0));
        state.finish();
        assert(state.restore_pending);
        // Reentry before a safe restoration keeps the same original choice.
        state.start(-1);
        assert(state.active && !state.restore_pending);
        assert(state.restore_character == character);
        state.clear(); // Retry/world retirement cancels the old phase.
        assert(!state.active && !state.restore_pending && state.restore_character == -1);
    }
    CharacterAscensionState sora;
    sora.start(-1);
    assert(sora.active && !sora.allows_target(10, 0));
    sora.finish();
    assert(!sora.restore_pending);
}
