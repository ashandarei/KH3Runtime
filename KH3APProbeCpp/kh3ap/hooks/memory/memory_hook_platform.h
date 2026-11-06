#pragma once

// Unreal's S macro conflicts with PolyHook's template parameter names. Keep the
// complete detour type available before inline state destructors are instantiated.
#ifdef S
#pragma push_macro("S")
#undef S
#define KH3AP_MEMORY_RESTORE_S_AFTER_POLYHOOK
#endif
#include <polyhook2/Detour/x64Detour.hpp>
#ifdef KH3AP_MEMORY_RESTORE_S_AFTER_POLYHOOK
#pragma pop_macro("S")
#undef KH3AP_MEMORY_RESTORE_S_AFTER_POLYHOOK
#endif
