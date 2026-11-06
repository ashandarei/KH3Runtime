#include "kh3ap/features/world/portals/gate_opener_policy.h"
#include <cassert>

int main() {
    using kh3ap::gate_opener::State;
    State state;
    const std::set<int64_t> receipts{0, 4, 20};
    assert(state.available(receipts) == 3);
    assert(!state.consume(14, receipts));
    assert(!state.consume(1, receipts));
    assert(!state.consume(4, {}));
    assert(state.consume(4, receipts));
    assert(state.opened(4));
    assert(!state.consume(4, receipts));
    assert(state.available(receipts) == 2);
    // Reconnect/rollback uses the same receipt identities, never fresh charges.
    State restored = state;
    assert(restored.available({0}) == 0);
    assert(restored.available(receipts) == 2);
    assert(restored.consume(6, receipts));
    assert(restored.consume(13, receipts));
    assert(!restored.consume(3, receipts));
    assert(!restored.opened(14));
    assert(restored.spent.size() == 3);
}
