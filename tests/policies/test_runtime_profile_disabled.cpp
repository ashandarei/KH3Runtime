#include "kh3ap/diagnostics/profiling/reflection_profile.h"

static_assert(KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS == 0);

int main()
{
    // This target links without UE4SS, logging, collectors or hitch tracing.
    kh3ap::record_runtime_profile_sample(L"disabled", 50000);
    kh3ap::record_runtime_reflection_profile_sample(L"disabled", 50000);
    kh3ap::flush_runtime_profiles();
    const kh3ap::RuntimeReflectionProfileScope<false> scope{L"disabled"};
}
