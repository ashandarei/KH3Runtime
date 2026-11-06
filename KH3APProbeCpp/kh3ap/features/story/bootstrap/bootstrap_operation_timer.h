#pragma once
#include "kh3ap/app/config.h"
#include <DynamicOutput/DynamicOutput.hpp>
#include <UnrealDef.hpp>
#include <chrono>
#include <string_view>
namespace kh3ap
{
    // These operations may run only once at startup, so emit each timing
    // directly rather than waiting for another five-second profile bucket.
    struct BootstrapOperationTimer
    {
        std::wstring_view operation;
        std::wstring_view subject;
        std::chrono::steady_clock::time_point started{
            settings::s_enable_runtime_hot_path_profiling
                ? std::chrono::steady_clock::now()
                : std::chrono::steady_clock::time_point{}};

        ~BootstrapOperationTimer()
        {
            if constexpr (settings::s_enable_runtime_hot_path_profiling)
            {
                const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
                                         std::chrono::steady_clock::now() - started)
                                         .count();
                RC::Output::send<RC::LogLevel::Verbose>(
                    STR("[KH3APProbeCpp] WorldBootstrapTiming operation={} subject={} "
                        "elapsed_us={}\n"),
                    operation, subject, elapsed);
            }
        }
    };

} // namespace kh3ap
