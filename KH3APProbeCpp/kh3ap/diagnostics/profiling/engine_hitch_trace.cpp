#include "kh3ap/app/config.h"
#include "kh3ap/diagnostics/profiling/engine_hitch_trace.h"

#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
#include <chrono>
#include "kh3ap/diagnostics/character_swap_trace.h"
#include "kh3ap/bridge/transport/bridge_channels.h"
#include <DynamicOutput/DynamicOutput.hpp>
#include <File/Macros.hpp>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <array>
#include <sstream>

namespace kh3ap
{
    using ::kh3ap::settings::s_runtime_hitch_baseline_multiplier_denominator;
    using ::kh3ap::settings::s_runtime_hitch_baseline_multiplier_numerator;
    using ::kh3ap::settings::s_runtime_hitch_log_interval;
    using ::kh3ap::settings::s_runtime_hitch_min_frame_gap;
    using ::kh3ap::settings::s_runtime_hitch_warmup_samples;

    namespace
    {
        constexpr auto hitch_trace_minimum_microseconds = 1000;

        struct SlowScope
        {
            std::array<wchar_t, 192> name{};
            uint64_t us{};
            auto observe(std::wstring_view value, uint64_t elapsed) -> void
            {
                if (elapsed <= us) return;
                us = elapsed;
                const auto count = std::min(value.size(), name.size() - 1);
                std::copy_n(value.data(), count, name.data());
                name[count] = 0;
            }
        };

        struct Monitor
        {
            RuntimeEngineHitchState state{
                static_cast<uint64_t>(s_runtime_hitch_min_frame_gap.count()),
                s_runtime_hitch_baseline_multiplier_numerator,
                s_runtime_hitch_baseline_multiplier_denominator,
                s_runtime_hitch_warmup_samples};
            bool active{}, in_callback{};
            std::optional<RuntimeEngineFrameSample> last_frame;
            SlowScope engine_scope{}, callback_scope{}, previous_callback_scope{};
            std::optional<RuntimeEngineHitchEvidence> pending{};
            std::wstring pending_context{}, pending_scopes{};
            float pending_delta{};
            uint64_t interval_begin_us{}, pending_capture_us{}, hitch_count{};
        };
        thread_local Monitor monitor;

        auto now_us() -> uint64_t
        {
            return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count());
        }
        auto thread_cpu_us(uint64_t& value) -> bool
        {
            FILETIME created{}, exited{}, kernel{}, user{};
            if (!GetThreadTimes(GetCurrentThread(), &created, &exited, &kernel, &user)) return false;
            const auto ticks = [](FILETIME v) {
                return (static_cast<uint64_t>(v.dwHighDateTime) << 32) | v.dwLowDateTime;
            };
            value = (ticks(kernel) + ticks(user)) / 10;
            return true;
        }
        auto foreground() -> bool
        {
            DWORD pid{};
            GetWindowThreadProcessId(GetForegroundWindow(), &pid);
            return pid == GetCurrentProcessId();
        }
        auto scope_summary() -> std::wstring
        {
            std::wostringstream out;
            const auto append = [&out](std::wstring_view phase, const SlowScope& scope) {
                out << phase << L":" << (scope.us ? scope.name.data() : L"<none>") << L"=" << scope.us << L"us;";
            };
            append(L"previous_callback", monitor.previous_callback_scope);
            append(L"engine", monitor.engine_scope);
            append(L"current_callback", monitor.callback_scope);
            return out.str();
        }
    }

    RuntimeEngineHitchScope::RuntimeEngineHitchScope(float delta_seconds, bool idle)
        : delta_seconds_(delta_seconds)
    {
        // Nested engine ticks must not replace the outer frame's timeline.
        if (monitor.active) return;
        active_ = monitor.active = true;
        character_swap_trace::frame_begin();
        monitor.in_callback = false;
        monitor.engine_scope = {};
        monitor.callback_scope = {};
        frame_.begin_us = now_us();
        frame_.cpu_valid = thread_cpu_us(frame_.begin_cpu_us);
        frame_.foreground = foreground();
        frame_.idle = idle;
    }

    auto RuntimeEngineHitchScope::engine_finished() -> void
    {
        if (!active_) return;
        frame_.engine_end_us = now_us();
        frame_.cpu_valid = thread_cpu_us(frame_.engine_end_cpu_us) && frame_.cpu_valid;
        engine_finished_ = true;
        monitor.in_callback = true;
        character_swap_trace::engine_finished();
    }

    auto RuntimeEngineHitchScope::set_context_provider(
            const std::function<std::wstring()>* provider) -> void
    {
        context_provider_ = provider;
    }

    RuntimeEngineHitchScope::~RuntimeEngineHitchScope() noexcept
    {
        if (!active_) return;
        try
        {
            if (engine_finished_)
            {
                character_swap_trace::frame_end();
                frame_.callback_end_us = now_us();
                frame_.cpu_valid = thread_cpu_us(frame_.callback_end_cpu_us) && frame_.cpu_valid;
                // Capture before formatting/logging. Diagnostic output belongs
                // to between_ticks_us on the next frame, never engine_tick_us.
                const auto evidence = monitor.state.observe(frame_);
                monitor.last_frame = frame_;
                monitor.active = false;
                if (monitor.interval_begin_us == 0) monitor.interval_begin_us = frame_.callback_end_us;
                if (evidence)
                {
                    ++monitor.hitch_count;
                    if (!monitor.pending || evidence->engine_end_gap_us > monitor.pending->engine_end_gap_us)
                    {
                        monitor.pending = evidence;
                        monitor.pending_capture_us = frame_.callback_end_us;
                        monitor.pending_delta = delta_seconds_;
                        monitor.pending_scopes = scope_summary();
                        monitor.pending_context = context_provider_ && *context_provider_
                            ? (*context_provider_)() : L"<unavailable>";
                    }
                }
                monitor.previous_callback_scope = monitor.callback_scope;
                if (frame_.callback_end_us - monitor.interval_begin_us >=
                    static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
                        s_runtime_hitch_log_interval).count()))
                {
                    if (monitor.pending)
                    {
                        const auto& e = *monitor.pending;
                        RC::Output::send<RC::LogLevel::Warning>(
                            STR("[KH3APProbeCpp] RuntimeEngineHitch count={} sequence={} baseline_us={} threshold_us={} engine_end_gap_us={} previous_callback_us={} between_ticks_us={} engine_tick_us={} current_callback_us={} gap_thread_cpu_us={} gap_cpu_valid={} engine_thread_cpu_us={} callback_thread_cpu_us={} tick_cpu_valid={} previous_foreground={} foreground={} previous_idle={} idle={} delta_seconds={} captured_age_us={} scopes={} context={}\n"),
                            monitor.hitch_count, e.sequence, e.baseline_us, e.threshold_us,
                            e.engine_end_gap_us, e.previous_callback_us, e.between_ticks_us,
                            e.engine_tick_us, e.callback_us, e.gap_thread_cpu_us, e.gap_cpu_valid,
                            e.engine_thread_cpu_us, e.callback_thread_cpu_us, e.tick_cpu_valid,
                            e.previous_foreground, e.foreground, e.previous_idle, e.idle,
                            monitor.pending_delta,
                            frame_.callback_end_us - monitor.pending_capture_us,
                            monitor.pending_scopes, monitor.pending_context);
                    }
                    monitor.pending.reset();
                    monitor.hitch_count = 0;
                    monitor.interval_begin_us = frame_.callback_end_us;
                }
            }
        }
        catch (...) { /* Diagnostics must not escape the engine tick boundary. */ }
        monitor.active = false;
    }

    auto record_engine_hitch_scope(std::wstring_view name, uint64_t elapsed_us) -> void
    {
        character_swap_trace::record_scope(name, elapsed_us);
        if (!monitor.active || elapsed_us < hitch_trace_minimum_microseconds || name == L"on_update") return;
        (monitor.in_callback ? monitor.callback_scope : monitor.engine_scope).observe(name, elapsed_us);
    }

    auto runtime_engine_last_frame_sample() -> std::optional<RuntimeEngineFrameSample>
    {
        return monitor.last_frame;
    }
}
#endif
