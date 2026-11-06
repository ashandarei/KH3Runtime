#pragma once
#include "kh3ap/core/performance_build.h"
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
#include <nlohmann/json_fwd.hpp>
#include <cstdint>
#include <chrono>
#include <string_view>

namespace kh3ap::character_swap_trace
{
    // Explicit, one-swap diagnostic. Reuses the engine-tick boundary and
    // UE4SS's existing ProcessEvent hooks; never discovers objects while idle.
    void arm();
    void begin(int player_id);
    void frame_begin();
    void engine_finished();
    void frame_end();
    void record_scope(std::wstring_view name, uint64_t elapsed_us);
    class NativeScope
    {
      public:
        explicit NativeScope(std::string_view name);
        ~NativeScope();
        NativeScope(const NativeScope&) = delete;
        NativeScope& operator=(const NativeScope&) = delete;

      private:
        std::string_view name_;
        std::chrono::steady_clock::time_point started_{};
        size_t phase_{};
        bool active_{};
    };
    nlohmann::json snapshot();
}
#endif
