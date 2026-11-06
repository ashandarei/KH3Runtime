#include "kh3ap/features/colors/color_apply_state.h"
#include "kh3ap/core/objects/uobject_validity.h"
#include <cassert>
#include <iostream>

namespace kh3ap
{
    auto is_uobject_valid_fast(const RC::Unreal::UObject* object) noexcept -> bool
    {
        return object && object->alive;
    }
}

int main()
{
    kh3ap::RuntimeColorApplyState state;
    RC::Unreal::UObject pawn, weapon, pending;
    const auto now = std::chrono::steady_clock::now();
    state.mark_colored(&pawn);
    state.mark_weapon_colored(&weapon);
    state.queue_pending(&pending, L"old-character", now);
    assert(state.is_colored(&pawn) && state.is_weapon_colored(&weapon));
    assert(state.is_pending(&pending));
    // A new object at exactly the same address must not inherit any state.
    ++pawn.serial;
    ++weapon.serial;
    ++pending.serial;
    assert(!state.is_colored(&pawn) && !state.is_weapon_colored(&weapon));
    assert(!state.is_pending(&pending));
    assert(state.colored().empty() && state.colored_weapons().empty());
    state.prune_expired();
    assert(state.pending().empty());
    state.mark_colored(&pawn);
    state.mark_colored(&pawn);
    state.mark_weapon_colored(&weapon);
    state.queue_pending(&pending, L"replacement", now);
    assert(state.colored().size() == 1 && state.colored_weapons().size() == 1);
    assert(state.pending().size() == 1 && state.pending()[0].character_key == L"replacement");
    pawn.alive = weapon.alive = pending.alive = false;
    state.prune_expired();
    assert(state.colored().empty() && state.colored_weapons().empty() && state.pending().empty());
    pawn.alive = true;
    state.mark_colored(&pawn);
    state.reset_for_map_jump();
    assert(!state.is_colored(&pawn));
    std::cout << "Color lifetime regression tests passed\n";
}
