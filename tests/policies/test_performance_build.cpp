#include "kh3ap/core/conditional_profile_scope.h"
#include "kh3ap/diagnostics/profiling/engine_hitch_trace.h"

#include <type_traits>

static_assert(KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS == TEST_EXPECT_DIAGNOSTICS);

int main()
{
#if TEST_EXPECT_DIAGNOSTICS
    // Diagnostic scopes retain timing/name storage and their recording destructor.
    static_assert(!std::is_empty_v<kh3ap::RuntimeHotPathProfileScope>);
    static_assert(!std::is_trivially_destructible_v<kh3ap::RuntimeHotPathProfileScope>);
    static_assert(!std::is_empty_v<kh3ap::RuntimeEngineHitchScope>);
    static_assert(!std::is_trivially_destructible_v<kh3ap::RuntimeEngineHitchScope>);
#else
    // The default player build must link without the profiler implementation:
    // even direct/explicitly enabled scope call sites cannot time or record work.
    static_assert(std::is_empty_v<kh3ap::RuntimeHotPathProfileScope>);
    static_assert(std::is_trivially_destructible_v<kh3ap::RuntimeHotPathProfileScope>);
    static_assert(std::is_trivially_destructible_v<kh3ap::RuntimeConditionalProfileScope<true>>);
    static_assert(std::is_empty_v<kh3ap::RuntimeEngineHitchScope>);
    static_assert(std::is_trivially_destructible_v<kh3ap::RuntimeEngineHitchScope>);
    const kh3ap::RuntimeEngineHitchScope engine{0.0167f, false};
    const kh3ap::RuntimeHotPathProfileScope direct{L"player_scope_long_enough_to_allocate_if_enabled"};
    const kh3ap::RuntimeConditionalProfileScope<true> nested{L"player_scope"};
#endif
}
