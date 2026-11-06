#pragma once

#include "kh3ap/core/performance_build.h"
#include <DynamicOutput/DynamicOutput.hpp>
#include <UnrealDef.hpp>
#include <chrono>
#include <cstdint>
#include <string_view>

namespace kh3ap::party_detail
{
    using namespace RC;
    struct PartyOperationTimer
    {
        static constexpr bool profiling_enabled = KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS != 0;
        std::wstring_view operation;
        std::wstring_view member;
        int32_t slot;
        std::chrono::steady_clock::time_point started{
            profiling_enabled ? std::chrono::steady_clock::now()
                              : std::chrono::steady_clock::time_point{}};
        ~PartyOperationTimer()
        {
            if constexpr (profiling_enabled)
            {
                Output::send<LogLevel::Verbose>(
                    STR("[KH3APProbeCpp] PartyTiming operation={} member={} slot={} "
                        "elapsed_us={}\n"),
                    operation, member, slot,
                    std::chrono::duration_cast<std::chrono::microseconds>(
                        std::chrono::steady_clock::now() - started)
                        .count());
            }
        }
    };
} // namespace kh3ap::party_detail
