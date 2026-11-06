#pragma once

#include <algorithm>
#include <cstdint>
#include <optional>

namespace kh3ap
{
    inline constexpr auto hitch_baseline_sample_weight = 32;

    // All timestamps are from one thread and one monotonic clock. The gap
    // between engine returns is exactly previous callback + between ticks +
    // current engine tick. Current callback belongs to the following gap.
    struct RuntimeEngineFrameSample
    {
        uint64_t begin_us{}, engine_end_us{}, callback_end_us{};
        uint64_t begin_cpu_us{}, engine_end_cpu_us{}, callback_end_cpu_us{};
        bool cpu_valid{};
        bool foreground{};
        bool idle{};
    };

    struct RuntimeEngineHitchEvidence
    {
        uint64_t sequence{}, baseline_us{}, threshold_us{};
        uint64_t engine_end_gap_us{}, previous_callback_us{}, between_ticks_us{};
        uint64_t engine_tick_us{}, callback_us{};
        uint64_t gap_thread_cpu_us{}, engine_thread_cpu_us{}, callback_thread_cpu_us{};
        bool gap_cpu_valid{}, tick_cpu_valid{};
        bool previous_foreground{}, foreground{}, previous_idle{}, idle{};
    };

    class RuntimeEngineHitchState
    {
    public:
        RuntimeEngineHitchState(uint64_t minimum_us, uint64_t numerator, uint64_t denominator,
                                uint64_t warmup)
            : minimum_us_(minimum_us), numerator_(numerator), denominator_(denominator),
              warmup_(warmup)
        {
        }

        auto observe(const RuntimeEngineFrameSample& frame)
            -> std::optional<RuntimeEngineHitchEvidence>
        {
            // Reject recursion, clock reversal and incomplete frames rather
            // than subtracting unsigned timestamps and inventing a giant hitch.
            if (frame.engine_end_us < frame.begin_us ||
                frame.callback_end_us < frame.engine_end_us ||
                (previous_ && frame.begin_us < previous_->callback_end_us))
            {
                return std::nullopt;
            }

            ++sequence_;
            std::optional<RuntimeEngineHitchEvidence> result;
            if (previous_)
            {
                const auto& prev = *previous_;
                const auto gap = frame.engine_end_us - prev.engine_end_us;
                if (baseline_us_ == 0)
                {
                    baseline_us_ = gap;
                }
                const auto threshold =
                    std::max(minimum_us_, baseline_us_ * numerator_ / denominator_);
                if (sequence_ > warmup_ && gap >= threshold)
                {
                    const bool tick_cpu = frame.cpu_valid &&
                                          frame.begin_cpu_us <= frame.engine_end_cpu_us &&
                                          frame.engine_end_cpu_us <= frame.callback_end_cpu_us;
                    const bool gap_cpu = tick_cpu && prev.cpu_valid &&
                                         prev.engine_end_cpu_us <= frame.engine_end_cpu_us;
                    result = RuntimeEngineHitchEvidence{
                        sequence_,
                        baseline_us_,
                        threshold,
                        gap,
                        prev.callback_end_us - prev.engine_end_us,
                        frame.begin_us - prev.callback_end_us,
                        frame.engine_end_us - frame.begin_us,
                        frame.callback_end_us - frame.engine_end_us,
                        gap_cpu ? frame.engine_end_cpu_us - prev.engine_end_cpu_us : 0,
                        tick_cpu ? frame.engine_end_cpu_us - frame.begin_cpu_us : 0,
                        tick_cpu ? frame.callback_end_cpu_us - frame.engine_end_cpu_us : 0,
                        gap_cpu,
                        tick_cpu,
                        prev.foreground,
                        frame.foreground,
                        prev.idle,
                        frame.idle};
                }
                else if (gap < threshold)
                {
                    baseline_us_ = (baseline_us_ * (hitch_baseline_sample_weight - 1) + gap) /
                                   hitch_baseline_sample_weight;
                }
            }
            previous_ = frame;
            return result;
        }

    private:
        uint64_t minimum_us_, numerator_, denominator_, warmup_;
        uint64_t sequence_{}, baseline_us_{};
        std::optional<RuntimeEngineFrameSample> previous_{};
    };
} // namespace kh3ap
