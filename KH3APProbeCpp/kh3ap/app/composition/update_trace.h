#pragma once
#include "kh3ap/app/composition/settings.h"
#include "kh3ap/diagnostics/profiling/reflection_profile.h"
#include "kh3ap/diagnostics/profiling/engine_hitch_trace.h"
#include <DynamicOutput/DynamicOutput.hpp>
#include <algorithm>
#include <array>
#include <chrono>
#include <sstream>
#include <string>
#include <string_view>

namespace kh3ap::composition_detail
{
    using namespace settings;
    using namespace RC;
    template <bool Enabled> class RuntimeProfileScope;

    template <> class RuntimeProfileScope<false>
    {
    public:
        explicit RuntimeProfileScope(std::wstring_view)
        {
        }

        RuntimeProfileScope(std::wstring_view, std::wstring_view)
        {
        }
    };

    template <> class RuntimeProfileScope<true>
    {
    public:
        explicit RuntimeProfileScope(std::wstring_view scope_name)
            : scope_name(scope_name), started_at(std::chrono::steady_clock::now())
        {
        }

        RuntimeProfileScope(std::wstring_view prefix, std::wstring_view scope_name)
            : scope_name(std::wstring{prefix} + std::wstring{scope_name}),
              started_at(std::chrono::steady_clock::now())
        {
        }

        RuntimeProfileScope(const RuntimeProfileScope&) = delete;
        auto operator=(const RuntimeProfileScope&) -> RuntimeProfileScope& = delete;

        ~RuntimeProfileScope()
        {
            const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now() - started_at);
            record_runtime_profile_sample(scope_name, static_cast<uint64_t>(elapsed.count()));
        }

    private:
        std::wstring scope_name;
        std::chrono::steady_clock::time_point started_at{};
    };

#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
    class OnUpdatePhaseTrace
    {
    public:
        OnUpdatePhaseTrace()
        {
            if constexpr (s_enable_runtime_hot_path_profiling || s_enable_runtime_hitch_diagnostics)
            {
                started_at = std::chrono::steady_clock::now();
                last_mark = started_at;
            }
        }

        auto mark(std::wstring_view phase_name) -> void
        {
            if constexpr (!s_enable_runtime_hot_path_profiling &&
                          !s_enable_runtime_hitch_diagnostics)
            {
                return;
            }
            if (phase_count >= phases.size())
            {
                return;
            }
            const auto now = std::chrono::steady_clock::now();
            phases[phase_count++] = Phase{
                .name = phase_name,
                .elapsed_since_previous_us = static_cast<uint64_t>(
                    std::chrono::duration_cast<std::chrono::microseconds>(now - last_mark).count()),
            };
            last_mark = now;
        }

        ~OnUpdatePhaseTrace()
        {
            if constexpr (!s_enable_runtime_hot_path_profiling &&
                          !s_enable_runtime_hitch_diagnostics)
            {
                return;
            }
            const auto total_us =
                static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
                                          std::chrono::steady_clock::now() - started_at)
                                          .count());
            if constexpr (s_enable_runtime_hitch_diagnostics)
            {
                record_hitch_sample(total_us);
            }
            if constexpr (s_enable_runtime_hot_path_profiling)
            {
                // Reuse the hitch trace's existing timestamps to attribute
                // ordinary idle work, too. Aggregate after the outer update
                // scope closes so these diagnostic writes do not distort it.
                for (size_t index = 0; index < phase_count; ++index)
                {
                    const auto& phase = phases[index];
                    record_runtime_profile_sample(std::wstring{STR("on_update_phase:")} +
                                                      std::wstring{phase.name},
                                                  phase.elapsed_since_previous_us);
                }
                if (total_us <
                    static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
                                              s_runtime_profile_slow_log_threshold)
                                              .count()))
                {
                    return;
                }
                Output::send<LogLevel::Warning>(
                    STR("[KH3APProbeCpp] OnUpdateSlowPhases total_us={} phases={}\n"), total_us,
                    format_phase_summary());
            }
        }

    private:
        struct Phase
        {
            std::wstring_view name{};
            uint64_t elapsed_since_previous_us{};
        };

        struct HitchAggregate
        {
            std::chrono::steady_clock::time_point previous_update_started_at{};
            std::chrono::steady_clock::time_point interval_started_at{};
            uint64_t sample_count{};
            uint64_t baseline_gap_us{};
            uint64_t frame_gap_count{};
            uint64_t slow_work_count{};
            uint64_t worst_frame_gap_us{};
            uint64_t worst_work_us{};
            std::wstring worst_work_phases{};
        };

        auto format_phase_summary() const -> std::wstring
        {
            std::wostringstream summary;
            for (size_t index = 0; index < phase_count; ++index)
            {
                if (index > 0)
                {
                    summary << STR(", ");
                }
                summary << phases[index].name << STR("=") << phases[index].elapsed_since_previous_us
                        << STR("us");
            }
            return summary.str();
        }

        auto record_hitch_sample(uint64_t total_us) const -> void
        {
            static HitchAggregate aggregate{};
            const auto now = std::chrono::steady_clock::now();
            if (aggregate.interval_started_at == std::chrono::steady_clock::time_point{})
            {
                aggregate.interval_started_at = now;
            }

            uint64_t frame_gap_us = 0;
            if (aggregate.previous_update_started_at != std::chrono::steady_clock::time_point{})
            {
                frame_gap_us =
                    static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
                                              started_at - aggregate.previous_update_started_at)
                                              .count());
            }
            aggregate.previous_update_started_at = started_at;
            ++aggregate.sample_count;

            if (frame_gap_us > 0)
            {
                if (aggregate.baseline_gap_us == 0)
                {
                    aggregate.baseline_gap_us = frame_gap_us;
                }
                const uint64_t minimum_gap_us =
                    static_cast<uint64_t>(s_runtime_hitch_min_frame_gap.count());
                const uint64_t adaptive_gap_us =
                    std::max(minimum_gap_us, aggregate.baseline_gap_us *
                                                 s_runtime_hitch_baseline_multiplier_numerator /
                                                 s_runtime_hitch_baseline_multiplier_denominator);
                const bool warmed_up = aggregate.sample_count > s_runtime_hitch_warmup_samples;
                if (warmed_up && frame_gap_us >= adaptive_gap_us)
                {
                    ++aggregate.frame_gap_count;
                    aggregate.worst_frame_gap_us =
                        std::max(aggregate.worst_frame_gap_us, frame_gap_us);
                }
                else if (frame_gap_us < adaptive_gap_us)
                {
                    aggregate.baseline_gap_us =
                        (aggregate.baseline_gap_us * 31 + frame_gap_us) / 32;
                }
            }

            if (total_us >= static_cast<uint64_t>(s_runtime_hitch_work_threshold.count()))
            {
                ++aggregate.slow_work_count;
                if (total_us > aggregate.worst_work_us)
                {
                    aggregate.worst_work_us = total_us;
                    aggregate.worst_work_phases = format_phase_summary();
                }
            }

            if ((now - aggregate.interval_started_at) < s_runtime_hitch_log_interval)
            {
                return;
            }
            if (aggregate.frame_gap_count > 0 || aggregate.slow_work_count > 0)
            {
                Output::send<LogLevel::Warning>(
                    STR("[KH3APProbeCpp] RuntimeHitchSummary samples={} baseline_gap_us={} "
                        "frame_gap_count={} worst_frame_gap_us={} slow_work_count={} "
                        "worst_work_us={} worst_work_phases={}\n"),
                    aggregate.sample_count, aggregate.baseline_gap_us, aggregate.frame_gap_count,
                    aggregate.worst_frame_gap_us, aggregate.slow_work_count,
                    aggregate.worst_work_us,
                    aggregate.worst_work_phases.empty() ? STR("<none>")
                                                        : aggregate.worst_work_phases);
            }
            aggregate.interval_started_at = now;
            aggregate.frame_gap_count = 0;
            aggregate.slow_work_count = 0;
            aggregate.worst_frame_gap_us = 0;
            aggregate.worst_work_us = 0;
            aggregate.worst_work_phases.clear();
        }

        std::chrono::steady_clock::time_point started_at{};
        std::chrono::steady_clock::time_point last_mark{};
        static constexpr size_t phase_capacity = 64;
        std::array<Phase, phase_capacity> phases{};
        size_t phase_count{};
    };

#else
    // No phase buffer initialization or timestamp reads in player builds.
    class OnUpdatePhaseTrace
    {
    public:
        auto mark(std::wstring_view) -> void
        {
        }
    };
#endif

} // namespace kh3ap::composition_detail
