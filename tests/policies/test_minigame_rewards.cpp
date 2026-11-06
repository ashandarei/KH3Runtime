#include "kh3ap/features/story/minigames/minigame_reward_rules.h"
#include <cassert>
#include <string>
int main() {
    using namespace kh3ap;
    // Values extracted independently from TresRemuneration_Data.
    static_assert(minigame_reward_crc(L"RA_MINIGAME_Rank_A_1st")==3631653579u);
    static_assert(minigame_reward_crc(L"TS_MINIGAME_Rank_A")==2588965220u);
    static_assert(minigame_reward_crc(L"FZ_MINIGAME_TREASURE")==3115261971u);
    static_assert(minigame_reward_crc(L"BX_MINIGAME_1_Rank_A")==3704587400u);
    static_assert(minigame_reward_crc(L"BX_MINIGAME_2_Rank_A")==4110889082u);
    const auto actor=L"ra_02_mission_Dance_After_C /Game/Levels/ra/ra_02/umap/ra_02_mission_Dance_After.ra_02_mission_Dance_After:PersistentLevel.ra_02_mission_Dance_After_C_1";
    assert(minigame_reward_caller_matches(L"RA_MINIGAME_Rank_A",actor));
    assert(!minigame_reward_caller_matches(L"TS_MINIGAME_Rank_A",actor));
    assert(!minigame_reward_caller_matches(L"RA_MINIGAME_Rank_E",actor));
    assert(!minigame_reward_caller_matches(L"RA_MINIGAME_TREASURE",actor));
    assert(!minigame_reward_caller_matches(L"RA_MINIGAME_Rank_A",L"TresRemunerationStatics /Script/TresGame.Default__TresRemunerationStatics"));
    assert(!minigame_reward_caller_matches(L"RA_MINIGAME_Rank_A",L"BlueprintGeneratedClass /Game/Levels/ra/ra_02/umap/ra_02_mission_Dance_After.ra_02_mission_Dance_After_C"));
    assert(minigame_reward_owner(L"BX_MINIGAME_3_Rank_A").empty());
    assert(minigame_reward_owner(L"FZ_MINIGAME_TREASURE_extra").empty());
    PoohPanaceaBonus bonus;
    bonus.observe(false,true); // Loading a result screen is not a new round.
    assert(!bonus.pending);
    PoohPanaceaBonus native;
    for(int phase=0;phase<5;++phase)native.observe_phase(phase);
    native.observe_phase(19); // Merely loading a result cannot earn a bonus.
    assert(!native.pending);
    native.observe_phase(5);
    native.observe_phase(7);
    native.observe_phase(19); // Actual gameplay followed by native completion.
    assert(native.pending);
    native.leave_game();
    assert(native.pending); // Return transition must preserve the owed reward.
    native.paid();
    native.observe_phase(19);
    assert(!native.pending);
    native.observe_phase(5);
    native.observe_phase(19); // A second ordinary round pays again.
    assert(native.pending);
    native.paid();
    native.observe_phase(5);
    native.leave_game(); // An abandoned round never pays.
    native.observe_phase(19);
    assert(!native.pending);
    bonus.observe(true,false);
    bonus.observe(false,true); // Completion pays even when ingredient stock is full.
    assert(bonus.pending);
    bonus.paid();
    bonus.observe(false,true); // A persistent result screen pays only once.
    assert(!bonus.pending);
    bonus.observe(true,false); // Replay.
    bonus.observe(false,true);
    assert(bonus.pending);
    bonus.leave_game(); // Defer payment until playable Sora returns.
    assert(bonus.pending);
    bonus.paid();
    bonus.observe(true,false);
    bonus.leave_game(); // Abandoning an unfinished round never pays.
    assert(!bonus.pending);
}
