#pragma once
// Unreal's S macro collides with PolyHook template parameters.
#ifdef S
#pragma push_macro("S")
#undef S
#define KH3AP_OPTIONS_RESTORE_S
#endif
#include <polyhook2/Detour/x64Detour.hpp>
#ifdef KH3AP_OPTIONS_RESTORE_S
#pragma pop_macro("S")
#undef KH3AP_OPTIONS_RESTORE_S
#endif
