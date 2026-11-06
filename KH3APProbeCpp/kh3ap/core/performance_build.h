#pragma once

// One local-testing switch controls performance diagnostics and agent bridge
// automation. Player builds exclude both, including standalone header consumers.
#ifndef KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
#define KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS 0
#endif

#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS != 0 && KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS != 1
#error KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS must be 0 or 1
#endif
