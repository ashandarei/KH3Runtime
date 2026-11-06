#pragma once

#include "kh3ap/core/performance_build.h"
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>

#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
#include "kh3ap/diagnostics/profiling/engine_hitch_state.h"
#endif

namespace kh3ap
{
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
    // Reuse the existing engine-tick timestamps in bounded native tests.
    auto runtime_engine_last_frame_sample() -> std::optional<RuntimeEngineFrameSample>;
#endif
    class RuntimeEngineHitchScope
    {
    public:
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
        RuntimeEngineHitchScope(float delta_seconds, bool idle);
        ~RuntimeEngineHitchScope() noexcept;
        auto engine_finished() -> void;
        auto set_context_provider(const std::function<std::wstring()>* provider) -> void;
        RuntimeEngineHitchScope(const RuntimeEngineHitchScope&) = delete;
        auto operator=(const RuntimeEngineHitchScope&) -> RuntimeEngineHitchScope& = delete;
    private:
        RuntimeEngineFrameSample frame_{};
        float delta_seconds_{};
        bool active_{};
        bool engine_finished_{};
        const std::function<std::wstring()>* context_provider_{};
#else
        RuntimeEngineHitchScope(float, bool) {}
        auto engine_finished() -> void {}
        auto set_context_provider(const std::function<std::wstring()>*) -> void {}
#endif
    };

#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
    // Reuses already enabled profiler scopes; installs no additional hooks.
    auto record_engine_hitch_scope(std::wstring_view name, uint64_t elapsed_us) -> void;
#endif
}
