#include "kh3ap/features/world/startup/fast_start_policy.h"

using namespace kh3ap;
// An autosave alone and manual-only catalogs must both block, regardless of
// startup timing or whether the save manager is still processing the catalog.
static_assert(fast_start_eligibility(1, 14, true) == FastStartEligibility::existing);
static_assert(fast_start_eligibility(9, 19, true) == FastStartEligibility::existing);
static_assert(fast_start_eligibility(1, 4, false) == FastStartEligibility::existing);
// Zero is not proof of a fresh game until the catalog initialization finishes.
static_assert(fast_start_eligibility(0, 4, true) == FastStartEligibility::waiting);
static_assert(fast_start_eligibility(0, 14, false) == FastStartEligibility::waiting);
static_assert(fast_start_eligibility(-1, 19, true) == FastStartEligibility::waiting);
// System settings alone must not prevent first-run preferences or New Game.
static_assert(fast_start_eligibility(0, 8, true) == FastStartEligibility::empty);
static_assert(fast_start_eligibility(0, 14, true) == FastStartEligibility::empty);
static_assert(fast_start_eligibility(0, 19, true) == FastStartEligibility::empty);
// A later populated catalog revokes eligibility instead of caching empty.
static_assert(fast_start_eligibility(1, 19, true) != FastStartEligibility::empty);
int main()
{
}
