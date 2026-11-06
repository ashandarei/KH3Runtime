#pragma once

#include <array>

namespace kh3ap
{
    enum class RuntimeCommandSource
    {
        Recovery,
        Diagnostic
    };

    // Player recovery remains available independently of agent diagnostics.
    inline constexpr std::array runtime_command_sources{
        RuntimeCommandSource::Recovery,
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
        RuntimeCommandSource::Diagnostic,
#endif
    };

    inline constexpr auto runtime_command_source_allowed(bool recovery) -> bool
    {
        return recovery || KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS != 0;
    }
} // namespace kh3ap
