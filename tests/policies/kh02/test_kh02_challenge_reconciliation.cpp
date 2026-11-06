#include "kh3ap/features/kh02/challenges/kh02_challenge_reconciliation.h"
#include <cassert>

int main()
{
    using namespace kh3ap;
    kh02_progress::Flags flags;
    std::map<int, int> challenges, counts;
    constexpr int canary = 1'000;
    challenges[canary] = 123;
    counts[canary] = 456;
    counts[kh02_challenge_id::destroy_thorn] = kh02_tracking::goal(kh02_challenge_id::destroy_thorn) + 10;
    challenges[kh02_challenge_id::get_white_gem_mirror_area] = kh02_challenge_state::complete;
    kh02_reconcile_challenges(flags, challenges, counts);
    assert(counts[kh02_challenge_id::destroy_thorn] == kh02_tracking::goal(kh02_challenge_id::destroy_thorn));
    assert(challenges[kh02_challenge_id::destroy_thorn] == kh02_challenge_state::complete);
    assert(counts[kh02_challenge_id::get_white_gem_mirror_area] == 0);
    assert(challenges[kh02_challenge_id::get_white_gem_mirror_area] == kh02_challenge_state::in_progress);
    assert(challenges[canary] == 123 && counts[canary] == 456);

    for (const auto* id : {"DW_LBOX01", "DW_BOX003", "DW_BOX004", "DW_BOX006", "DW_BOX007"})
    {
        flags[std::string("gameflow_DW_CHALLENGE_CHEST_") + id] = 1;
    }
    for (const auto* id : {"DW_BOX008", "DW_LBOX02", "DW_BOX009", "DW_BOX011", "DW_BOX013", "DW_BOX033", "DW_BOX015"})
    {
        flags[std::string("gameflow_DW_CHALLENGE_CHEST_") + id] = 1;
    }
    for (int gem = 1; gem <= 7; ++gem)
    {
        flags["gameflow_CHALLENGE_29_0" + std::to_string(gem)] = 1;
    }
    const auto source_flags = flags;
    kh02_reconcile_challenges(flags, challenges, counts);
    assert(flags == source_flags);
    assert(counts[kh02_challenge_id::get_all_treasure_gear_area] == 5);
    assert(challenges[kh02_challenge_id::get_all_treasure_gear_area] == kh02_challenge_state::complete);
    assert(counts[kh02_challenge_id::get_all_treasure_mirror_area] == 7);
    assert(challenges[kh02_challenge_id::get_all_treasure_mirror_area] == kh02_challenge_state::in_progress);
    assert(counts[kh02_challenge_id::get_white_gem_mirror_area] == 7);
    assert(challenges[kh02_challenge_id::get_white_gem_mirror_area] == kh02_challenge_state::complete);
    flags.erase("gameflow_CHALLENGE_29_07");
    kh02_reconcile_challenges(flags, challenges, counts);
    assert(counts[kh02_challenge_id::get_white_gem_mirror_area] == 6);
    assert(challenges[kh02_challenge_id::get_white_gem_mirror_area] == kh02_challenge_state::in_progress);
    const auto stable_challenges = challenges;
    const auto stable_counts = counts;
    kh02_reconcile_challenges(flags, challenges, counts);
    assert(challenges == stable_challenges && counts == stable_counts);
}
