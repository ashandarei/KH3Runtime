#include "kh3ap/features/player/controller_abilities/controller_ability_refresh_state.h"
#include "kh3ap/features/player/keyblades/style_change_mode.h"
#include "kh3ap/features/player/controller_abilities/magic_link_gates.h"

#include <cassert>
#include <chrono>

int main()
{
    using kh3ap::resolve_runtime_magic_link_gates;
    for (bool magic : {false, true})
    for (bool links : {false, true})
    {
        const auto split = resolve_runtime_magic_link_gates(magic, links, false, false);
        assert(split.mp_enabled == (magic || links));
        assert(split.magic_restricted == !magic);
        assert(split.links_restricted == !links);
        const auto story = resolve_runtime_magic_link_gates(magic, links, true, true);
        assert(story.magic_restricted && story.links_restricted);
    }
    using Mode = kh3ap::RuntimeStyleChangeMode;
    using kh3ap::resolve_runtime_style_change_mode;
    assert(resolve_runtime_style_change_mode(std::nullopt) == Mode::PerKeyblade);
    assert(resolve_runtime_style_change_mode(0) == Mode::Shared);
    assert(resolve_runtime_style_change_mode(1) == Mode::PerKeyblade);
    assert(resolve_runtime_style_change_mode(2) == Mode::PerKeyblade);
    assert(resolve_runtime_style_change_mode(99) == Mode::PerKeyblade);
    using State = kh3ap::RuntimeControllerAbilityRefreshState;
    using namespace std::chrono_literals;
    State state;
    const auto start = State::Clock::time_point{};
    assert(!state.begin(start, 0));
    assert(!state.begin(start + 1h, 0));
    assert(state.begin(start, 9)); // Coalesced menu and native ability events.
    assert(state.events() == 9 && state.attempts() == 1);
    state.finish(true);
    assert(!state.pending());
    assert(!state.begin(start + 30s, 0)); // No old periodic reapply.
    assert(!state.begin(start + 1h, 0));

    assert(state.begin(start, 4));
    state.finish(false);
    assert(!state.begin(start + 249ms, 0));
    for (unsigned i = 1; i < State::maximum_attempts; ++i)
    {
        assert(state.begin(start + i * State::retry_delay, 0));
        state.finish(false);
    }
    assert(!state.pending());
    assert(!state.begin(start + 1h, 0)); // Exhaustion cannot become polling.

    assert(state.begin(start + 1h, 8)); // A later event rearms readiness work.
    assert(state.attempts() == 1 && state.events() == 8);
    state.finish(false);
    assert(state.begin(start + 1h + 1ms, 16)); // New event need not wait.
    assert(state.events() == 24 && state.attempts() == 1);
    state.finish(true);
    assert(!state.begin(start + 2h, 0));

    kh3ap::RuntimeControllerAbilityAppliedState applied;
    applied.record(6, true);
    applied.record(7, false);
    applied.invalidate(6); // A story setter cleared Magic, but did not change Links.
    assert(applied.needs_update(6, true));
    assert(!applied.needs_update(7, false));
    const std::array<bool, 5> before{true, false, true, false, true};
    for (size_t i = 0; i < before.size(); ++i)
    {
        assert(applied.needs_update(i, before[i])); // New controller initialization.
        applied.record(i, before[i]);
    }
    const auto update = [&](const auto& desired, int failed_index = -1) {
        unsigned calls = 0;
        for (size_t i = 0; i < desired.size(); ++i)
        {
            if (!applied.needs_update(i, desired[i])) continue;
            ++calls;
            if (static_cast<int>(i) != failed_index) applied.record(i, desired[i]);
        }
        return calls;
    };
    assert(update(before) == 0); // Closing a menu without changes is a no-op.
    auto after = before;
    after[0] = false;
    assert(update(after) == 1); // Unequip only changes its own effect.
    after[1] = true;
    after[2] = false;
    assert(update(after, 2) == 2); // Partial failure cannot lose the pending change.
    assert(update(after) == 1); // Retry only the failed effect.
    assert(update(after) == 0); // Coalesced/duplicate notifications do not reapply.
    assert(update(before) == 3); // Re-equipping restores the prior effects.
    applied.reset(); // A grant or a genuinely new controller forces initialization.
    assert(update(before) == before.size());
    assert(update(before) == 0);
    assert(!applied.reset_for_map_ready(L"/Game/Levels/ew/ew_01/ew_01.PersistentLevel.Sora"));
    assert(!applied.reset_for_map_ready(L"/Game/Levels/he/he_020/he_020.PersistentLevel.Sora"));
    assert(update(before) == 0);
    assert(applied.reset_for_map_ready(L"/Game/Levels/he/he_02/he_02.PersistentLevel.Sora"));
    assert(update(before, 2) == before.size()); // Same controller/abilities must reapply on arrival.
    assert(update(before) == 1); // Only failed Magic retries; no recurring reapply.
    assert(update(before) == 0);
}
