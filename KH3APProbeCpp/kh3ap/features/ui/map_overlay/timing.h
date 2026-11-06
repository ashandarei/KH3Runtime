#pragma once
#include "kh3ap/core/performance_build.h"
#include "kh3ap/features/ui/map_overlay/settings.h"
#include <DynamicOutput/DynamicOutput.hpp>
#include <UnrealDef.hpp>
#include <chrono>
namespace kh3ap::map_overlay_detail
{
    using namespace RC;
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
    struct MapTickTimer
    {
        std::chrono::steady_clock::time_point start{std::chrono::steady_clock::now()};
        bool report{};
        const wchar_t* operation{L"update"};
        double movies_ms{}, chests_ms{}, save_points_ms{}, image_ms{}, metadata_ms{}, layout_ms{};
        ~MapTickTimer()
        {
            const auto ms =
                std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start)
                    .count();
            if (report || ms >= map_overlay_settings::slow_update_warning_ms)
            {
                Output::send<LogLevel::Warning>(
                    STR("[KH3APProbeCpp] Area map timing operation={} total_ms={} movies_ms={} "
                        "chests_ms={} save_points_ms={} image_ms={} metadata_ms={} layout_ms={}\n"),
                    operation, ms, movies_ms, chests_ms, save_points_ms, image_ms, metadata_ms,
                    layout_ms);
            }
        }
    };
    struct MinimapDisplayTimer
    {
        using Clock = std::chrono::steady_clock;
        Clock::time_point start{Clock::now()}, previous{start};
        bool first{};
        double context_ms{}, activation_ms{}, projection_ms{}, collection_ms{}, draw_ms{},
            reveal_ms{};
        auto stage(double& target) -> void
        {
            const auto now = Clock::now();
            target = std::chrono::duration<double, std::milli>(now - previous).count();
            previous = now;
        }
        ~MinimapDisplayTimer()
        {
            const auto ms = std::chrono::duration<double, std::milli>(Clock::now() - start).count();
            if (first || ms >= map_overlay_settings::slow_update_warning_ms)
            {
                Output::send<LogLevel::Warning>(
                    STR("[KH3APProbeCpp] Minimap display timing first={} total_ms={} context_ms={} "
                        "activation_ms={} projection_ms={} collection_ms={} draw_ms={} "
                        "reveal_ms={}\n"),
                    first, ms, context_ms, activation_ms, projection_ms, collection_ms, draw_ms,
                    reveal_ms);
            }
        }
    };
#endif

} // namespace kh3ap::map_overlay_detail
