#include "kh3ap/features/world/garden_return/alternate_interaction_policy.h"
#include <cassert>

int main()
{
    using namespace kh3ap;
    using alternate_interaction::accepts_player_state;
    for (int state = 0; state < 256; ++state)
    {
        const bool ground = state == player_state_ids::idle || state == player_state_ids::run;
        assert(accepts_player_state(state, false) == ground);
        assert(accepts_player_state(state, true) == (ground || state == player_state_ids::swim));
    }
    // Swimming combat and diving/falling do not become interaction fallbacks.
    assert(!accepts_player_state(player_state_ids::combo_swim, true));
    assert(!accepts_player_state(player_state_ids::open_treasure_box, true));
    assert(!accepts_player_state(player_state_ids::check, true));
}
