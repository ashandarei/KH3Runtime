#include "kh3ap/features/player/objects/object_observation_state.h"

namespace kh3ap
{
    auto RuntimeObjectObservationState::should_register_static_construct_callback(
            bool callback_enabled,
            bool observer_installed) const noexcept -> bool
    {
        return callback_enabled &&
               observer_installed &&
               !static_construct_callback_registered_;
    }

    auto RuntimeObjectObservationState::mark_static_construct_callback_registered()
            noexcept -> void
    {
        static_construct_callback_registered_ = true;
    }

    auto RuntimeObjectObservationState::static_construct_callback_registered() const
            noexcept -> bool
    {
        return static_construct_callback_registered_;
    }

    auto make_runtime_object_observation_drain_plan(
            bool discard,
            size_t observation_count,
            uint64_t dropped_count) noexcept
            -> RuntimeObjectObservationDrainPlan
    {
        return {
                .report_overflow = dropped_count != 0,
                .dispatch_observations = !discard && observation_count != 0,
        };
    }
}
