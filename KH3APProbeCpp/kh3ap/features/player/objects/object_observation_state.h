#pragma once

#include <cstddef>
#include <cstdint>

namespace kh3ap
{
    struct RuntimeObjectObservationDrainPlan
    {
        bool report_overflow{};
        bool dispatch_observations{};
    };

    // Process-lifetime state for UE4SS's append-only static-construct callback
    // registry plus the pure decision used when draining queued observations.
    class RuntimeObjectObservationState
    {
    public:
        [[nodiscard]] auto should_register_static_construct_callback(
                bool callback_enabled,
                bool observer_installed) const noexcept -> bool;
        auto mark_static_construct_callback_registered() noexcept -> void;

        [[nodiscard]] auto static_construct_callback_registered() const noexcept
                -> bool;

    private:
        bool static_construct_callback_registered_{};
    };

    [[nodiscard]] auto make_runtime_object_observation_drain_plan(
            bool discard,
            size_t observation_count,
            uint64_t dropped_count) noexcept
            -> RuntimeObjectObservationDrainPlan;
}
