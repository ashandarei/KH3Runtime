#pragma once
#include "kh3ap/core/performance_build.h"
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
#include <cstdint>
#include <string_view>

namespace RC::Unreal
{
    class UObject;
}

namespace kh3ap::kh02_mirror_transition_diagnostics
{
    void observe(RC::Unreal::UObject* world, RC::Unreal::UObject* script, int32_t linkage,
                 std::string_view stage, std::string_view mirror_event = {});
    auto pending() -> bool;
    void tick();
} // namespace kh3ap::kh02_mirror_transition_diagnostics
#endif
