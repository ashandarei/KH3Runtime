#include "../../../KH3APProbeCpp/kh3ap/features/kh02/challenges/kh02_challenge_combat_rules.h"
#include "../../../KH3APProbeCpp/kh3ap/features/kh02/challenges/kh02_challenge_encounter_sites.h"
#include <cassert>

int main()
{
    using namespace kh3ap;
    using namespace kh02_tracking;
    std::map<int, int> states, counts;
    assert(!add(states, counts, -1) && states.empty() && counts.empty());
    assert(!add(states, counts, kh02_challenge_id::kill_shadow, 0) && states.empty());
    assert(!shotlock_result(states, counts, false));
    assert(counts.contains(kh02_challenge_id::excellent_shoot));
    assert(states.at(kh02_challenge_id::excellent_shoot) == kh02_challenge_state::in_progress);
    for (int count = 0; count < excellent_shotlock_chain_goal; ++count)
    {
        assert(shotlock_result(states, counts, true));
    }
    assert(!shotlock_result(states, counts, false));
    assert(counts.at(kh02_challenge_id::excellent_shoot) == excellent_shotlock_chain_goal);
    states.clear();
    counts.clear();
    Hit unowned;
    // Native species defeat counts apply independently of final-hit attribution.
    assert(defeat(states, counts, L"e_ex001_Pawn_C", unowned));
    assert(counts.at(kh02_challenge_id::kill_shadow) == 1);
    assert(!counts.contains(kh02_challenge_id::firaga_finish));
    assert(species(L"e_ex001_PawnWrong") == -1);
    assert(!heartless(L"e_dw402_PawnWrong"));
    Hit finisher;
    finisher.player = finisher.lethal = finisher.magic = finisher.airborne = finisher.counter = finisher.style = true;
    finisher.element = first_magic_element;
    assert(defeat(states, counts, L"e_ex001_Pawn_C", finisher));
    assert(counts.at(kh02_challenge_id::firaga_finish) == 1);
    assert(counts.at(kh02_challenge_id::flight_battle) == 1);
    assert(counts.at(kh02_challenge_id::baria_finish) == 1);
    states.clear();
    counts.clear();
    assert(!level(states, counts, maximum_player_level + 1) && states.empty() && counts.empty());
    assert(level(states, counts, maximum_player_level));
    assert(!level(states, counts, maximum_player_level));
    using native_sites::Boundary;
    using native_sites::Operation;
    using native_sites::Site;
    const auto highest_camera = native_sites::site(
        L"/Game/Zevels/dw/dw_02/umap/dw_02_camera_ms200.dw_02_camera_ms200_C:ExecuteUbergraph_dw_02_camera_ms200");
    assert(highest_camera == Site::castle_highest_camera);
    assert(
        native_sites::accepts_castle(Operation::status, kh02_challenge_id::reaching_highest_gear_area, highest_camera));
    assert(!native_sites::accepts_castle(Operation::complete, kh02_challenge_id::reaching_highest_gear_area,
                                         highest_camera));
    assert(
        !native_sites::accepts_castle(Operation::start, kh02_challenge_id::reaching_highest_gear_area, highest_camera));
    assert(!native_sites::accepts_castle(Operation::status, kh02_challenge_id::look_shooting_star, highest_camera));
    assert(native_sites::site(L"/Game/Zevels/dw/dw_02/umap/dw_02_camera_ms200_fake.Graph") == Site::unknown);
    assert(native_sites::accepts_castle(Operation::status, kh02_challenge_id::look_shooting_star, Site::shooting_star));
    assert(
        !native_sites::accepts_castle(Operation::complete, kh02_challenge_id::look_shooting_star, Site::shooting_star));
    assert(
        !native_sites::accepts_castle(Operation::unsupported, kh02_challenge_id::kill_shadow, Site::castle_challenge));
    assert(native_sites::accepts_no_damage(kh02_challenge_id::no_damage_kill_darkside, Boundary::begin,
                                           Site::forest_challenge));
    assert(!native_sites::accepts_no_damage(kh02_challenge_id::no_damage_kill_darkside, Boundary::finish,
                                            Site::forest_gameflow));
    NoDamageAttempt attempt;
    assert(!attempt.finish());
    attempt.begin();
    attempt.damage(1);
    attempt.begin();
    assert(!attempt.finish()); // A repeated begin cannot erase damage already taken.
    attempt.begin();
    attempt.damage(0);
    assert(attempt.finish());
    assert(!attempt.finish());
}
