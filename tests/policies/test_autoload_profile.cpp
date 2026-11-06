#include "kh3ap/features/world/startup/autoload_profile.h"

using namespace kh3ap;
static_assert(autoload_store(0x60547D11, 0xAEBF000) == AutoloadStore::egs);
static_assert(autoload_store(0x66743F34, 0xA969000) == AutoloadStore::steam);
static_assert(autoload_store(0x60547D11, 0xA969000) == AutoloadStore::unsupported);
static_assert(autoload_store(0x66743F34, 0xAEBF000) == AutoloadStore::unsupported);
static_assert(autoload_store(0, 0) == AutoloadStore::unsupported);
static_assert(autoload_rva(AutoloadStore::steam, 0x18fd380) == 0x159d9e0);
static_assert(autoload_rva(AutoloadStore::egs, 0x18fd380) == 0x18fd380);
// Unported commands must never inherit Epic addresses on Steam.
static_assert(autoload_rva(AutoloadStore::steam, 0x18c8ce0) == 0);
static_assert(autoload_rva(AutoloadStore::steam, 0x18fcf60) == 0);
static_assert(autoload_rva(AutoloadStore::unsupported, 0x18fd380) == 0);
static_assert(native_load_selection(false, true, 0, 19, 70, 50) == NativeLoadSelection::waiting);
static_assert(native_load_selection(true, true, 0, 19, 70, 50) == NativeLoadSelection::waiting);
static_assert(native_load_selection(true, true, 1, 19, 50, 50) == NativeLoadSelection::accepted);
static_assert(native_load_selection(true, true, 1, 19, 70, 50) == NativeLoadSelection::mismatch);
static_assert(native_load_selection(true, false, 0, 19, 50, 50) == NativeLoadSelection::accepted);
static_assert(native_load_selection(true, true, 0, 25, 70, 50) == NativeLoadSelection::waiting);
static_assert(native_load_selection(true, true, 1, 19, 70, -1) == NativeLoadSelection::accepted);
static_assert(native_load_selection(true, true, 1, 19, 100, -1) == NativeLoadSelection::mismatch);
static_assert(native_load_selection(true, true, 1, 19, -1, -1) == NativeLoadSelection::mismatch);

constexpr bool retained_load_latches_first_selection() {
    int observed_slot = -1;
    if (observe_native_load_selection(observed_slot, false, true, 0, 19, 70, 50) != NativeLoadSelection::waiting ||
        observed_slot != -1) return false;
    // The gate may hold several polls while the manager still names an older save.
    for (int poll = 0; poll < 3; ++poll) {
        if (observe_native_load_selection(observed_slot, true, true, 0, 19, 70, 50) != NativeLoadSelection::waiting ||
            observed_slot != -1) return false;
    }
    if (observe_native_load_selection(observed_slot, true, true, 1, 19, 50, 50) != NativeLoadSelection::accepted ||
        observed_slot != 50) return false;
    // Completion clears the native operation slot before gameplay readiness.
    constexpr int reset_slots[]{-1, 0, 70};
    for (const int reset_slot : reset_slots) {
        if (observe_native_load_selection(observed_slot, true, false, 0, 19, reset_slot, 50) != NativeLoadSelection::accepted ||
            observed_slot != 50) return false;
    }
    return true;
}

constexpr bool first_mismatch_never_becomes_proof() {
    int observed_slot = -1;
    return observe_native_load_selection(observed_slot, true, true, 1, 19, 70, 50) == NativeLoadSelection::mismatch &&
           observed_slot == -1;
}

constexpr bool chooser_selection_survives_manager_reset() {
    int observed_slot = -1;
    // A fast load may leave title between polls; the chooser owns its slot choice.
    if (observe_native_load_selection(observed_slot, true, false, 0, 19, 99, -1) != NativeLoadSelection::accepted ||
        observed_slot != 99) return false;
    return observe_native_load_selection(observed_slot, true, false, 0, 19, -1, -1) == NativeLoadSelection::accepted &&
           observed_slot == 99;
}

constexpr bool successful_title_stage_preserves_selection() {
    int observed_slot = -1;
    // Title state 25 is a successful load stage, not a cancellation signal.
    if (observe_native_load_selection(observed_slot, true, true, 0, 25, 70, 50) != NativeLoadSelection::waiting ||
        observed_slot != -1) return false;
    if (observe_native_load_selection(observed_slot, true, true, 1, 19, 50, 50) != NativeLoadSelection::accepted) return false;
    return observe_native_load_selection(observed_slot, true, true, 0, 25, -1, 50) == NativeLoadSelection::accepted &&
           observed_slot == 50;
}

constexpr bool new_request_rechecks_selection() {
    int observed_slot = -1;
    if (observe_native_load_selection(observed_slot, true, false, 0, 19, 50, 50) != NativeLoadSelection::accepted) return false;
    // RuntimeAutoloadService resets this sole piece of selection state on accept.
    observed_slot = -1;
    return observe_native_load_selection(observed_slot, true, true, 1, 19, 50, 51) == NativeLoadSelection::mismatch &&
           observed_slot == -1;
}

static_assert(retained_load_latches_first_selection());
static_assert(first_mismatch_never_becomes_proof());
static_assert(chooser_selection_survives_manager_reset());
static_assert(successful_title_stage_preserves_selection());
static_assert(new_request_rechecks_selection());
// Only failures published after this dispatch can terminate its observation.
static_assert(!native_load_gate_failure_is_new(false, 0, 1, "provider_failed"));
static_assert(!native_load_gate_failure_is_new(true, 8, 7, "provider_failed"));
static_assert(!native_load_gate_failure_is_new(true, 8, 8, "provider_failed"));
static_assert(!native_load_gate_failure_is_new(true, 8, 9, ""));
static_assert(native_load_gate_failure_is_new(true, 8, 9, "provider_failed"));
static_assert(native_load_gate_failure_is_new(true, 8, 10, "provider_failed"));
int main() {}
