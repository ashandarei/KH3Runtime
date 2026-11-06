#include "kh3ap/features/kh02/story/kh02_source_script_registry.h"
#include <cassert>

int main()
{
    using namespace kh3ap;
    Kh02MapSessionState state;
    Kh02SourceScriptRegistry<int> registry(state);
    assert(registry.size() == 0);
    assert(!registry.accepts(0));
    registry.replace_world(10);
    const auto first_generation = registry.generation();
    assert(first_generation != 0 && registry.world() == 10);
    assert(registry.accepts_continuation(first_generation, true));
    assert(!registry.accepts_continuation(first_generation, false));
    const auto same_first = [](int previous) { return previous == 100; };
    assert(!registry.replacing(L"Flow", same_first));
    assert(registry.accepts_script(L"Flow", same_first)); // Loading-ready may precede the first script binding.
    assert(registry.size() == 0);                         // Observation must not insert a missing identity.
    registry.record(L"Flow", 100);
    registry.record(L"Camera", 200);
    assert(!registry.replacing(L"Flow", same_first));
    assert(registry.accepts_script(L"Flow", same_first));
    assert(registry.replacing(L"Flow", [](int previous) { return previous == 101; }));
    assert(!registry.accepts_script(L"Flow", [](int previous) { return previous == 101; }));
    assert(registry.replacing(L"Flow", [](int) { return false; })); // A weak reference that has expired.
    registry.invalidate();
    // The coordinator's retirement callback runs only after this invalidation.
    assert(!registry.accepts(first_generation));
    assert(registry.size() == 0 && registry.world() == 10);
    registry.record(L"Flow", 101);
    const auto retry_generation = registry.generation();
    registry.replace_world(11);
    assert(registry.world() == 11 && registry.size() == 0);
    assert(!registry.accepts(retry_generation));
    assert(registry.accepts(registry.generation()));
}
