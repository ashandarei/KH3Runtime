#include "kh3ap/features/kh02/challenges/kh02_challenge_policy.h"
#include <cassert>
#include <iostream>
using namespace kh3ap::kh02_tracking;
int main(){
    const auto castle=L"/Game/Zevels/dw/dw_02/umap/dw_02_challenge.dw_02_challenge_C:ExecuteUbergraph_dw_02_challenge";
    const auto star=L"/Game/Zlueprints/Gimmick/dw/g_dw_ShootingStar/g_dw_ShootingStar.g_dw_ShootingStar_C:ExecuteUbergraph_g_dw_ShootingStar";
    assert(castle_challenge_site(20,24,star));
    assert(!castle_challenge_site(21,24,star)&&!castle_challenge_site(22,24,star));
    assert(!castle_challenge_site(20,23,star));
    assert(castle_challenge_site(22,24,castle)&&castle_challenge_site(21,24,castle));
    assert(!castle_challenge_site(21,0,castle)&&!castle_challenge_site(22,50,castle));
    assert(!castle_challenge_site(21,24,L"/Game/Zevels/dw/dw_02/umap/dw_02_challenge_fake.Script"));
    std::map<int,int> states,counts;
    // Only the six specified families; close-looking names and non-Heartless
    // must not earn either species or elemental/aerial credit.
    assert(species(L"e_ex001_Pawn_s0_C")==0);
    assert(species(L"e_ex081_Pawn_C")==1);
    assert(species(L"e_ex003_Pawn_C")==2);
    assert(species(L"e_ex016_Pawn_s100_C")==3);
    assert(species(L"e_ex004_Pawn_C")==4);
    assert(species(L"e_ex005_Pawn_C")==5);
    assert(species(L"e_ex001_PawnFake_C")==-1);
    assert(!defeat(states,counts,L"e_dw405_Pawn_C",{true,true,true,1,true}));
    assert(counts.empty());
    assert(heartless(L"e_dw401_Pawn_C")&&heartless(L"e_dw402_Pawn_s0_C"));
    assert(!heartless(L"e_dw405_Pawn_C")&&!heartless(L"e_dw407a_Pawn_C"));
    assert(goal(38)==8); // includes the true chest among the mirror-room decoys

    // A party member's fire kill counts its species, never Sora's magic.
    assert(defeat(states,counts,L"e_ex001_Pawn_C",{false,true,true,1,true}));
    assert(counts[0]==1&&counts[10]==0&&counts[16]==0);
    assert(defeat(states,counts,L"e_ex001_Pawn_C",{true,true,true,1,true}));
    assert(counts[0]==2&&counts[10]==1&&counts[16]==1);
    defeat(states,counts,L"e_ex001_Pawn_C",{true,true,true,1,false});
    assert(counts[10]==1&&counts[16]==1); // a partial spell followed by generic damage is not a magic/aerial finish
    // Elemental Keyblade damage is not the original magic condition.
    defeat(states,counts,L"e_ex003_Pawn_C",{true,false,false,1,true});assert(counts[10]==1);
    for(int element=1;element<=3;++element){
        defeat(states,counts,L"e_ex004_Pawn_C",{true,true,false,element,true});
        assert(counts[9+element]>0);
    }
    for(int i=0;i<100;++i)defeat(states,counts,L"e_ex001_Pawn_C",{true,true,true,1,true});
    assert(counts[0]==30&&states[0]==2);
    assert(counts[10]==30&&states[10]==2);
    assert(counts[16]==5&&states[16]==2);
    assert(level(states,counts,70));
    assert(states[6]==2&&states[7]==2&&states[8]==1&&counts[8]==70);
    assert(!level(states,counts,0)&&!level(states,counts,100));
    assert(!level(states,counts,60)); // no loss from a form's stale level read
    assert(level(states,counts,80)&&states[8]==2);
    NoDamageAttempt attempt;
    assert(!attempt.finish());
    attempt.begin();attempt.damage(0);assert(attempt.finish());
    assert(!attempt.finish()); // repeated completion cannot become a new win
    attempt.begin();attempt.damage(1);assert(!attempt.finish());
    attempt.begin();attempt.damage(1);attempt.begin();assert(!attempt.finish()); // duplicate start cannot erase damage
    attempt.begin();attempt.reset();assert(!attempt.finish());
    assert(no_damage_site(40,true,L"/Game/Zevels/dw/dw_07/umap/dw_07_enemy.dw_07_enemy_C:ExecuteUbergraph"));
    assert(no_damage_site(45,true,L"/Game/Zevels/dw/dw_08/umap/dw_08_challenge.dw_08_challenge_C:ExecuteUbergraph"));
    assert(no_damage_site(45,false,L"/Game/Zevels/dw/dw_08/umap/dw_08_gameflow.dw_08_gameflow_C:ExecuteUbergraph"));
    assert(!no_damage_site(45,false,L"/Game/Zevels/dw/dw_08/umap/dw_08_challenge.dw_08_challenge_C:ExecuteUbergraph"));
    assert(no_damage_site(46,false,L"/Game/Zevels/dw/dw_08/umap/dw_08_enemy.dw_08_enemy_C:ExecuteUbergraph"));
    assert(!no_damage_site(46,true,L"/Game/Zevels/dw/dw_07/umap/dw_07_enemy.dw_07_enemy_C:ExecuteUbergraph"));
    std::map<int,NoDamageAttempt> overlapping;
    overlapping[45].begin();overlapping[46].begin();
    for(auto& [id,a]:overlapping)a.damage(3);
    assert(!overlapping[45].finish()&&!overlapping[46].finish());
    overlapping[45].begin();assert(overlapping[45].finish());

    std::map<int,int> forest_states,forest_counts;
    for(int i=0;i<51;++i)add(forest_states,forest_counts,32);
    assert(forest_counts[32]==50&&forest_states[32]==2);
    std::map<std::wstring,bool> forest_flags{{L"gameflow_CHALLENGE_34_RED_FLOWER",true}};
    auto forest_flag=[&](const wchar_t* key){return forest_flags[key];};
    assert(forest_outcomes(forest_states,forest_counts,forest_flag));
    assert(forest_counts[33]==1&&forest_states[33]==1&&forest_counts[43]==0);
    assert(!forest_outcomes(forest_states,forest_counts,forest_flag));
    forest_flags[L"gameflow_CHALLENGE_34_BLUE_FLOWER"]=true;
    forest_flags[L"gameflow_CHALLENGE_34_GREEN_FLOWER"]=true;
    for(auto chest:forest_chests)forest_flags[std::wstring(L"gameflow_DW_CHALLENGE_CHEST_")+std::wstring(chest)]=true;
    forest_flags[L"gameflow_CHALLENGE_45_dw_08_MEMORY"]=true;
    assert(forest_outcomes(forest_states,forest_counts,forest_flag));
    assert(forest_counts[33]==3&&forest_states[33]==2&&forest_counts[43]==12&&forest_states[43]==2&&forest_states[44]==2);
    forest_flags.erase(L"gameflow_DW_CHALLENGE_CHEST_DW_BOX035");
    assert(forest_outcomes(forest_states,forest_counts,forest_flag));
    assert(forest_counts[43]==11&&forest_states[43]==1); // distinct source identities, not repeated open events
    std::map<std::string,bool> flags{{"gameflow_DW_MIRROR_BATTLE01_CLEAR",true},
                                     {"BattleBonus_DW_dw_07_MirrorAqua_03",true}};
    assert(mirror_outcomes(states,[&](const char* key){return flags[key];}));
    assert(states[25]==2&&states[26]!=2&&states[40]!=2);
    assert(!mirror_outcomes(states,[&](const char* key){return flags[key];}));
    // A finisher's type and player attribution matter independently of species.
    std::map<int, int> combat_states, combat_counts;
    Hit counter{true, false, false, 0, true};
    counter.counter = true;
    for (int kill = 0; kill < 5; ++kill)
    {
        defeat(combat_states, combat_counts, L"e_ex001_Pawn_C", counter);
    }
    assert(combat_counts[9] == 5 && combat_states[9] == 2);
    Hit spellweaver{true, false, false, 0, true};
    spellweaver.spellweaver = spellweaver.style = true;
    assert(!defeat(combat_states, combat_counts, L"e_dw405_Pawn_C", spellweaver));
    assert(combat_states[30] != 2 && combat_counts[18] == 0);
    assert(spellweaver_activation(combat_states, combat_counts, false, false));
    assert(combat_states[19] == 2 && combat_states[20] != 2 && combat_states[30] != 2);
    assert(spellweaver_activation(combat_states, combat_counts, true, true));
    assert(combat_states[20] == 2 && combat_states[30] == 2); // spin activation, without a kill
    assert(defeat(combat_states, combat_counts, L"e_dw402_Pawn_C", spellweaver));
    assert(combat_states[35] == 2 && combat_counts[18] == 1);
    spellweaver.player = false;
    defeat(combat_states, combat_counts, L"e_dw402_Pawn_C", spellweaver);
    assert(combat_counts[18] == 1);
    for (int kill = 0; kill < 3; ++kill)
    {
        defeat(combat_states, combat_counts, L"e_dw407b_Pawn_C", counter);
    }
    assert(combat_counts[31] == 3 && combat_states[31] == 2);
    assert(!mickey_link_state(1) && !mickey_link_state(112));
    assert(mickey_link_state(113) && !mickey_link_state(114));
    assert(!critical_tide_victory(combat_states, 0));
    assert(!critical_tide_victory(combat_states, 1));
    assert(!critical_tide_victory(combat_states, 2));
    assert(combat_states[50] != 2);
    assert(critical_tide_victory(combat_states, 3) && combat_states[50] == 2);

    for (int shot = 0; shot < 5; ++shot)
    {
        shotlock_result(combat_states, combat_counts, true);
    }
    assert(combat_counts[17] == 5 && combat_states[17] != 2);
    assert(shotlock_result(combat_states, combat_counts, false));
    assert(combat_counts[17] == 0);
    for (int shot = 0; shot < 6; ++shot)
    {
        shotlock_result(combat_states, combat_counts, true);
    }
    assert(combat_counts[17] == 6 && combat_states[17] == 2);
    assert(!shotlock_result(combat_states, combat_counts, false));

    IceSlide slide;
    assert(slide.sample({0, 0, 0}) == 0);
    slide.active = true;
    slide.sample({0, 0, 0});
    for (int meter = 1; meter <= 75; ++meter)
    {
        assert(slide.sample({float(meter * 100), 0, 0}) == meter);
    }
    slide.reset();
    slide.active = true;
    slide.sample({0, 0, 0});
    assert(slide.sample({100000, 0, 0}) == 0 && !slide.active); // a warp cannot finish a slide
    slide.active = true;
    slide.sample({0, 0, 0});
    assert(slide.sample({1000, 0, 0}) == 10);
    slide.end();
    slide.active = true;
    slide.sample({5000, 0, 0});
    assert(slide.sample({5500, 0, 0}) == 15); // another segment continues the actual travelled distance

    FreezeCohorts cohorts;
    cohorts.observe({1, 2, 3, 4});
    assert(cohorts.shatter(1) == 0); // four frozen enemies cannot establish this objective
    cohorts.observe({1, 2, 3, 4, 5});
    assert(cohorts.shatter(1) == 1 && cohorts.shatter(1) == 1);
    assert(cohorts.shatter(2) == 2);
    cohorts.observe({3, 4, 5});
    assert(cohorts.shatter(3) == 3);
    cohorts.observe({6, 7, 8, 9, 10});
    assert(cohorts.shatter(6) == 1); // a new cohort does not inherit three old shatters
    for (uint64_t id = 7; id <= 10; ++id)
    {
        assert(cohorts.shatter(id) == static_cast<int>(id - 5));
    }

    std::map<std::wstring, bool> ending_flags;
    auto ending_flag = [&](const std::wstring& key) { return ending_flags[key]; };
    assert(ending_outcomes(combat_states, combat_counts, ending_flag));
    assert(combat_counts[42] == 0 && combat_counts[47] == 0);
    for (auto chest : zodiac_chests)
    {
        ending_flags[L"gameflow_DW_CHALLENGE_CHEST_" + std::wstring(chest)] = true;
    }
    assert(ending_outcomes(combat_states, combat_counts, ending_flag));
    assert(combat_counts[42] == 12 && combat_states[42] == 2 && combat_states[47] != 2);
    assert(!ending_outcomes(combat_states, combat_counts, ending_flag)); // revisits do not count twice
    for (auto chest : depths_chests)
    {
        ending_flags[L"gameflow_DW_CHALLENGE_CHEST_" + std::wstring(chest)] = true;
    }
    ending_flags[L"gameflow_CHALLENGE_49_dw_09_MEMORY"] = true;
    assert(ending_outcomes(combat_states, combat_counts, ending_flag));
    assert(combat_counts[47] == 5 && combat_states[47] == 2 && combat_states[48] == 2);
    std::cout<<"KH02 enemy identity, attribution, element, aerial and level checks passed\n";
}
