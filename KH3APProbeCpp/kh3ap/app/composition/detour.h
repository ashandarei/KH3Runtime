#pragma once
// Unreal's S macro conflicts with PolyHook template parameters. Retry observer
// storage also needs the complete detour type before its destructor is emitted.
#ifdef S
#pragma push_macro("S")
#undef S
#define KH3AP_COMPOSITION_RESTORE_S
#endif
#include <polyhook2/Detour/x64Detour.hpp>
#ifdef KH3AP_COMPOSITION_RESTORE_S
#pragma pop_macro("S")
#undef KH3AP_COMPOSITION_RESTORE_S
#endif
