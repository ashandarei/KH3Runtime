#pragma once
// PolyHook's template parameters must be parsed before Unreal's S macro.
#ifdef S
#pragma push_macro("S")
#undef S
#define KH3AP_AUTOSAVE_RESTORE_S
#endif
#include <polyhook2/Detour/x64Detour.hpp>
#ifdef KH3AP_AUTOSAVE_RESTORE_S
#pragma pop_macro("S")
#undef KH3AP_AUTOSAVE_RESTORE_S
#endif
