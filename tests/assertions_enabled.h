#pragma once

// Force-include this before each test translation unit. A local #undef in one
// source file must not hide a target whose other assertions compile away.
#ifdef NDEBUG
#error Native regression tests require assertions in every build configuration.
#endif
