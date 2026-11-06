#include "kh3ap/diagnostics/profiling/engine_hitch_state.h"
#include <cassert>

using namespace kh3ap;

namespace
{
    auto normal(uint64_t begin) -> RuntimeEngineFrameSample
    {
        return {begin, begin + 4000, begin + 4500,
                begin / 2, begin / 2 + 2000, begin / 2 + 2200, true, true, false};
    }

    auto warm(RuntimeEngineHitchState& state) -> uint64_t
    {
        for (uint64_t n = 0; n < 40; ++n)
            assert(!state.observe(normal(1000000 + n * 16667)));
        return 1000000 + 40 * 16667;
    }
}

int main()
{
    // Reproduce a 174 ms frame gap with little mod work. The engine's
    // 161 ms tick accounts for it; current callback must not be blamed.
    {
        RuntimeEngineHitchState state{25000, 5, 2, 30};
        const auto next = warm(state);
        auto frame = normal(next);
        frame.engine_end_us += 157333;
        frame.callback_end_us = frame.engine_end_us + 800;
        frame.engine_end_cpu_us = frame.begin_cpu_us + 120000;
        frame.callback_end_cpu_us = frame.engine_end_cpu_us + 200;
        const auto e = state.observe(frame);
        assert(e && e->engine_end_gap_us == 174000);
        assert(e->previous_callback_us == 500);
        assert(e->between_ticks_us == 12167);
        assert(e->engine_tick_us == 161333 && e->callback_us == 800);
        assert(e->engine_end_gap_us == e->previous_callback_us + e->between_ticks_us + e->engine_tick_us);
        assert(e->gap_cpu_valid && e->tick_cpu_valid);
        assert(e->engine_thread_cpu_us == 120000);
        // A hitch must not raise the adaptive baseline and hide the next one.
        assert(e->baseline_us == 16667 && e->threshold_us == 41667);
    }
    // Slow previous callback vs waiting between ticks are separate sources.
    {
        RuntimeEngineHitchState state{25000, 5, 2, 30};
        auto next = warm(state);
        auto prev = normal(next);
        prev.callback_end_us = prev.engine_end_us + 55000;
        prev.cpu_valid = false;
        assert(!state.observe(prev));
        auto frame = normal(prev.callback_end_us + 12000);
        frame.foreground = false;
        frame.idle = true;
        const auto e = state.observe(frame);
        assert(e && e->previous_callback_us == 55000 && e->between_ticks_us == 12000);
        assert(e->engine_tick_us == 4000);
        assert(!e->gap_cpu_valid && e->tick_cpu_valid);
        assert(e->previous_foreground && !e->foreground && !e->previous_idle && e->idle);
    }
    {
        RuntimeEngineHitchState state{25000, 5, 2, 30};
        auto next = warm(state);
        auto frame = normal(next + 160000);
        frame.cpu_valid = false;
        const auto e = state.observe(frame);
        assert(e && e->between_ticks_us == 172167 && e->engine_tick_us == 4000);
        assert(!e->tick_cpu_valid && !e->gap_cpu_valid);
    }
    // Invalid/recursive frames cannot underflow or replace the previous sample.
    {
        RuntimeEngineHitchState state{25000, 5, 2, 30};
        const auto next = warm(state);
        auto invalid = normal(next);
        invalid.engine_end_us = invalid.begin_us - 1;
        assert(!state.observe(invalid));
        assert(!state.observe(normal(next - 20000)));
        assert(!state.observe(normal(next)));
        auto reversed_cpu = normal(next + 174000);
        reversed_cpu.engine_end_cpu_us = 1;
        const auto e = state.observe(reversed_cpu);
        assert(e && !e->tick_cpu_valid && !e->gap_cpu_valid);
    }
    // 30 FPS needs a higher threshold; ordinary pacing is not a hitch.
    {
        RuntimeEngineHitchState state{25000, 5, 2, 30};
        for (uint64_t n = 0; n < 100; ++n)
            assert(!state.observe(normal(1000000 + n * 33333)));
        auto frame = normal(1000000 + 100 * 33333 + 40000);
        assert(!state.observe(frame));
    }
}
