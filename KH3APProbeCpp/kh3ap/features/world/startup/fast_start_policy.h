#pragma once
namespace kh3ap {
enum class FastStartEligibility { waiting, empty, existing };

// System/settings data is not a playable save. The native catalog contains
// actual game slots (autosave and manual), not the system-settings record.
constexpr FastStartEligibility fast_start_eligibility(int slots, int title_state, bool save_manager_idle) {
    if (slots > 0) return FastStartEligibility::existing;
    if (slots < 0 || !save_manager_idle) return FastStartEligibility::waiting;
    // The initial catalog read (title state 4) must finish before accepting an
    // empty catalog. These are its downstream setup/title presentation states.
    switch (title_state) {
    case 7: case 8: case 9: case 10: case 11: case 12: case 13: case 14:
    case 18: case 19: case 27: case 28: case 40: case 53: case 54:
        return FastStartEligibility::empty;
    default: return FastStartEligibility::waiting;
    }
}
}
