#pragma once
#include "kh3ap/core/performance_build.h"
#include <DynamicOutput/DynamicOutput.hpp>
#include <UnrealDef.hpp>
#include <chrono>
#include <string_view>

namespace kh3ap
{
    // Stage timings describe game-thread work, not client bridge latency or FPS.
    struct CharacterSwapOperationTimer
    {
        static constexpr bool enabled = KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS != 0;
        static constexpr auto minimum_report_time = std::chrono::milliseconds(1);
        std::wstring_view operation;
        int player_id;
        bool always_report{};
        std::chrono::steady_clock::time_point started{
            enabled ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{}};

        ~CharacterSwapOperationTimer()
        {
            if constexpr (enabled)
            {
                const auto elapsed = std::chrono::steady_clock::now() - started;
                if (always_report || elapsed >= minimum_report_time)
                {
                    RC::Output::send<RC::LogLevel::Normal>(
                        STR("[KH3APProbeCpp] CharacterSwapTiming operation={} id={} "
                            "elapsed_us={}\n"),
                        operation, player_id,
                        std::chrono::duration_cast<std::chrono::microseconds>(elapsed).count());
                }
            }
        }
    };
} // namespace kh3ap
