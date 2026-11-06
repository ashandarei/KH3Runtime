#include "kh3ap/features/player/state/sora_state_revalidation.h"

#include <cassert>

int main()
{
    using kh3ap::RuntimeSoraStateRevalidationResult;
    using kh3ap::RuntimeSoraStateRevalidationSnapshot;
    using kh3ap::classify_runtime_sora_state_revalidation;
    using kh3ap::runtime_sora_state_revalidation_invalidates_state_comp;
    using kh3ap::runtime_sora_state_revalidation_requires_cache_invalidation;

    assert(classify_runtime_sora_state_revalidation({}) ==
           RuntimeSoraStateRevalidationResult::CacheUnavailable);

    const RuntimeSoraStateRevalidationSnapshot base{
            .active_pawn = 0x1000,
            .cached_pawn = 0x1000,
            .state_comp = 0x2000,
            .state_comp_outer = 0x1000,
            .current_state = 0x3000,
            .cached_current_state = 0x3000,
            .current_state_id = 67,
            .cached_current_state_id = 67,
            .catalog_match = true,
    };

    assert(classify_runtime_sora_state_revalidation(base) ==
           RuntimeSoraStateRevalidationResult::Unchanged);

    auto changed = base;
    changed.current_state = 0x4000;
    changed.current_state_id = 1;
    assert(classify_runtime_sora_state_revalidation(changed) ==
           RuntimeSoraStateRevalidationResult::Changed);

    auto pawn_mismatch = base;
    pawn_mismatch.active_pawn = 0x5000;
    assert(classify_runtime_sora_state_revalidation(pawn_mismatch) ==
           RuntimeSoraStateRevalidationResult::PawnMismatch);

    auto owner_mismatch = base;
    owner_mismatch.state_comp_outer = 0x5000;
    assert(classify_runtime_sora_state_revalidation(owner_mismatch) ==
           RuntimeSoraStateRevalidationResult::StateCompOwnerMismatch);

    auto catalog_miss = base;
    catalog_miss.current_state = 0x4000;
    catalog_miss.current_state_id = 43;
    catalog_miss.catalog_match = false;
    assert(classify_runtime_sora_state_revalidation(catalog_miss) ==
           RuntimeSoraStateRevalidationResult::CatalogMiss);

    auto stable_uncataloged = catalog_miss;
    stable_uncataloged.cached_current_state = stable_uncataloged.current_state;
    stable_uncataloged.cached_current_state_id = stable_uncataloged.current_state_id;
    assert(classify_runtime_sora_state_revalidation(stable_uncataloged) ==
           RuntimeSoraStateRevalidationResult::Unchanged);

    assert(!runtime_sora_state_revalidation_requires_cache_invalidation(
            RuntimeSoraStateRevalidationResult::Unchanged));
    assert(!runtime_sora_state_revalidation_requires_cache_invalidation(
            RuntimeSoraStateRevalidationResult::Changed));
    assert(runtime_sora_state_revalidation_requires_cache_invalidation(
            RuntimeSoraStateRevalidationResult::CurrentStateUnavailable));
    assert(runtime_sora_state_revalidation_requires_cache_invalidation(
            RuntimeSoraStateRevalidationResult::StateCompOwnerMismatch));
    assert(!runtime_sora_state_revalidation_requires_cache_invalidation(
            RuntimeSoraStateRevalidationResult::CatalogMiss));

    assert(!runtime_sora_state_revalidation_invalidates_state_comp(
            RuntimeSoraStateRevalidationResult::CurrentStateUnavailable));
    assert(runtime_sora_state_revalidation_invalidates_state_comp(
            RuntimeSoraStateRevalidationResult::PawnMismatch));
    assert(runtime_sora_state_revalidation_invalidates_state_comp(
            RuntimeSoraStateRevalidationResult::StateCompOwnerMismatch));

    return 0;
}
