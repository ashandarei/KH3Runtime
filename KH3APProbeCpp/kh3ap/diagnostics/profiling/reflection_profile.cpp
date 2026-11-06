#include "kh3ap/app/config.h"
#include "kh3ap/diagnostics/profiling/reflection_profile.h"

#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
#include "kh3ap/bridge/transport/bridge_channels.h"
#include "kh3ap/diagnostics/profiling/runtime_profile_collector.h"
#include "kh3ap/diagnostics/profiling/engine_hitch_trace.h"

#include <File/Macros.hpp>
#include <DynamicOutput/DynamicOutput.hpp>

#endif

namespace kh3ap
{
    using ::kh3ap::settings::s_runtime_profile_log_interval;
    using ::kh3ap::settings::s_runtime_profile_slow_log_interval;
    using ::kh3ap::settings::s_runtime_profile_slow_log_threshold;

#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
    namespace
    {
        constexpr uint64_t runtime_slow_threshold_us =
                std::chrono::duration_cast<std::chrono::microseconds>(
                        s_runtime_profile_slow_log_threshold).count();

        struct RuntimeProfiles
        {
            RuntimeProfileCollector runtime{runtime_slow_threshold_us,
                    s_runtime_profile_slow_log_interval, s_runtime_profile_log_interval};
            // Reflection warnings historically have a 50 ms threshold and no throttle.
            RuntimeProfileCollector reflection{50000,
                    std::chrono::seconds{0}, s_runtime_profile_log_interval};
        };

        auto profiles() -> RuntimeProfiles&
        {
            static RuntimeProfiles value;
            return value;
        }

        auto log_reports(const std::vector<RuntimeProfileReport>& reports) -> void
        {
            for (const auto& report : reports)
            {
                RC::Output::send<RC::LogLevel::Warning>(
                        STR("[KH3APProbeCpp] Profile scope={} count={} avg_us={} max_us={} last_us={} interval_ms={}\n"),
                        report.scope_name, report.count, report.total_us / report.count,
                        report.max_us, report.last_us, report.interval_ms);
            }
        }
    }
#endif

    auto record_runtime_profile_sample(
            std::wstring_view scope_name, uint64_t elapsed_us) -> void
    {
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
        record_engine_hitch_scope(scope_name, elapsed_us);
        if (profiles().runtime.record(scope_name, elapsed_us, std::chrono::steady_clock::now()))
        {
            RC::Output::send<RC::LogLevel::Warning>(
                    STR("[KH3APProbeCpp] ProfileSlow scope={} elapsed_us={} threshold_us={}\n"),
                    scope_name, elapsed_us, runtime_slow_threshold_us);
        }
#endif
    }

    auto record_runtime_reflection_profile_sample(
            std::wstring_view scope_name, uint64_t elapsed_us) -> void
    {
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
        record_engine_hitch_scope(scope_name, elapsed_us);
        if (profiles().reflection.record(scope_name, elapsed_us, std::chrono::steady_clock::now()))
        {
            RC::Output::send<RC::LogLevel::Warning>(
                    STR("[KH3APProbeCpp] ProfileSlow scope={} elapsed_us={}\n"),
                    scope_name, elapsed_us);
        }
#endif
    }

    auto flush_runtime_profiles() -> void
    {
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
        const auto now = std::chrono::steady_clock::now();
        log_reports(profiles().runtime.flush_due(now));
        log_reports(profiles().reflection.flush_due(now));
#endif
    }
}
