#include "kh3ap/features/player/characters/character_trap_state.h"
#include "kh3ap/features/grants/trap_catalog.h"
#include <cassert>

int main()
{
    using namespace kh3ap;
    using character_swap::CharacterTrapState;
    CharacterTrapState trap;
    const auto started = CharacterTrapState::Clock::now();
    assert(!trap.locked());
    trap.prepare(13);
    assert(trap.locked() && trap.return_player == 13);
    assert(!trap.due(started + std::chrono::hours(1)));
    trap.activated(started);
    assert(!trap.due(started + std::chrono::milliseconds(9999)));
    assert(trap.due(started + std::chrono::seconds(10)));
    trap.phase = CharacterTrapState::Phase::Returning;
    assert(trap.locked()); // Input stays locked during deferred return readiness.
    trap.clear(); // Map travel cancels both the duration and input ownership.
    assert(!trap.locked() && !trap.due(started + std::chrono::hours(1)));
    assert(trap.return_player == -1);

    assert(find_runtime_trap_spec(king_mickey_trap_token, king_mickey_trap_method));
    assert(!find_runtime_trap_spec(king_mickey_trap_token, L"bp_tres_get_item"));
    assert(!find_runtime_trap_spec(L"ETresBadStatusType::TRES_BADSTAT_STUN", L"direct_player_bad_status"));
    assert(supports_alternate_bad_status(L"ETresBadStatusType::TRES_BADSTAT_BURN", L"direct_player_bad_status"));
    assert(supports_alternate_bad_status(L"ETresBadStatusType::TRES_BADSTAT_SNEEZE", L"direct_player_bad_status"));
    assert(!supports_alternate_bad_status(king_mickey_trap_token, king_mickey_trap_method));
    assert(!supports_alternate_bad_status(L"ETresBadStatusType::TRES_BADSTAT_BURN", L"bp_tres_get_item"));
    for (const int player : {10, 16, 17})
    {
        assert(alternate_bad_status_player_allowed(player));
    }
    for (const int player : {-1, 0, 8, 18})
    {
        assert(!alternate_bad_status_player_allowed(player));
    }
    assert(runtime_trap_state_allowed(1));
    assert(runtime_trap_state_allowed(3));
    for (const uint8_t state : {9, 41, 42, 59, 67, 74, 105, 106, 118})
    {
        assert(!runtime_trap_state_allowed(state));
    }
}
