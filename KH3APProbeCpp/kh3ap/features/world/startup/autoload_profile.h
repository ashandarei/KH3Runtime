#pragma once
#include "kh3ap/core/platform/game_build.h"
#include "kh3ap/features/world/save_slot_policy.h"

#include <cstdint>
#include <string_view>

namespace kh3ap {
// Native startup profiles are deliberately separate from storefront detection.
// Steam's manager layout and loading-task delegate ABI differ from EGS.
enum class AutoloadStore { unsupported, egs, steam };

enum class NativeLoadSelection { waiting, accepted, mismatch };

constexpr NativeLoadSelection native_load_selection(bool dispatched, bool title_active, int load_state,
                                                     int /*title_state*/, int manager_slot, int requested_slot) {
    if (!dispatched) return NativeLoadSelection::waiting;
    // A retained request has not written the manager's slot yet. Its old slot
    // must not be treated as evidence of either successful or incorrect loading.
    if (title_active && load_state <= 0) return NativeLoadSelection::waiting;
    if (!save_slots::is_native_slot(manager_slot) ||
        (requested_slot >= 0 && manager_slot != requested_slot)) return NativeLoadSelection::mismatch;
    return NativeLoadSelection::accepted;
}

constexpr NativeLoadSelection observe_native_load_selection(int& observed_slot, bool dispatched, bool title_active,
                                                             int load_state, int title_state, int manager_slot,
                                                             int requested_slot) {
    if (!dispatched) {
        return NativeLoadSelection::waiting;
    }
    // Native completion resets the operation slot during travel. Preserve the
    // first verified selection until the owning request resets observed_slot.
    if (observed_slot >= 0) {
        return NativeLoadSelection::accepted;
    }
    const auto selection = native_load_selection(dispatched, title_active, load_state, title_state,
                                                  manager_slot, requested_slot);
    if (selection == NativeLoadSelection::accepted) {
        observed_slot = manager_slot;
    }
    return selection;
}

constexpr bool native_load_gate_failure_is_new(bool dispatched, uint64_t dispatch_sequence,
                                               uint64_t failure_sequence, std::string_view reason) {
    return dispatched && failure_sequence > dispatch_sequence && !reason.empty();
}

constexpr AutoloadStore autoload_store(uint32_t timestamp, uint32_t size) {
    if (kh3ap::native_build::egs.matches(timestamp, size)) return AutoloadStore::egs;
    if (kh3ap::native_build::steam.matches(timestamp, size)) return AutoloadStore::steam;
    return AutoloadStore::unsupported;
}

constexpr uintptr_t autoload_rva(AutoloadStore store, uintptr_t egs) {
    if (store == AutoloadStore::egs) return egs;
    if (store != AutoloadStore::steam) return 0;
    switch (egs) {
    case 0x18fd380: return 0x159d9e0; // explicit slot load
    case 0x31aafa0: return 0x2d19720; // delegate heap storage free
    case 0x7c381e0: return 0x6a56078; // TresHUDTitle
    case 0x9c460d8: return 0x913c168; // native catalog (0xf88 records)
    case 0x9c460e0: return 0x913c170;
    case 0x7c6ad70: return 0x6b69538; // NowLoading
    case 0x1addef0: return 0x174a300;
    case 0x9c47a60: return 0x9a94db0; // loading operation singleton
    case 0x9c481c0: return 0x9a95820; // loading manager singleton
    case 0x18ec800: return 0x158fda0; // Continue completion action
    case 0x18eee00: return 0x158a8a0; // task removal
    case 0x191e8c0: return 0x15bf2d0; // loading thread/presentation cleanup
    case 0x7c40fa0: return 0x6a61110; // loading operation
    case 0x7c45c78: return 0x6a7cf00; // loading manager
    case 0x7c40f90: return 0x6a61228; // task-map shared owner
    case 0x7c45ce8: return 0x6a7d228; // loading thread
    case 0x7c41798: return 0x6a60e20; // scheduled task
    case 0x7c40ba8: return 0x6a61318; // Continue predicate delegate
    case 0x7c40d60: return 0x6a613f0; // Continue action delegate
    case 0x18ec6a0: return 0x158fcd0; // Continue predicate
    default: return 0; // Never reuse an unported Epic address on Steam.
    }
}
}
