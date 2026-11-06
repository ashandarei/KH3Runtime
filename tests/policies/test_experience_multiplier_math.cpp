#include "kh3ap/features/enemies/experience_multiplier_math.h"

#include <cassert>
#include <cstdint>
#include <limits>

int main()
{
    using kh3ap::experience_multiplier_choice_index;
    using kh3ap::experience_multiplier_half_steps_from_value;
    using kh3ap::is_supported_experience_multiplier_half_steps;
    using kh3ap::runtime_experience_multiplier_half_steps;
    using kh3ap::scale_experience_for_runtime_award;

    assert(runtime_experience_multiplier_half_steps.size() == 8);
    assert(is_supported_experience_multiplier_half_steps(1));
    assert(is_supported_experience_multiplier_half_steps(10));
    assert(is_supported_experience_multiplier_half_steps(20));
    assert(is_supported_experience_multiplier_half_steps(40));
    assert(!is_supported_experience_multiplier_half_steps(0));
    assert(!is_supported_experience_multiplier_half_steps(11));
    assert(experience_multiplier_choice_index(1) == 0);
    assert(experience_multiplier_choice_index(2) == 1);
    assert(experience_multiplier_choice_index(40) == 7);
    assert(experience_multiplier_choice_index(999) == 1);

    assert(experience_multiplier_half_steps_from_value(0.5).value() == 1);
    assert(experience_multiplier_half_steps_from_value(1.0).value() == 2);
    assert(experience_multiplier_half_steps_from_value(5.0).value() == 10);
    assert(experience_multiplier_half_steps_from_value(10.0).value() == 20);
    assert(experience_multiplier_half_steps_from_value(20.0).value() == 40);
    assert(!experience_multiplier_half_steps_from_value(0.0).has_value());
    assert(!experience_multiplier_half_steps_from_value(5.5).has_value());

    // Main scales vanilla awards directly rather than undoing a seed-baked
    // multiplier first. Pin rounding, invalid settings, and overflow at that API.
    assert(scale_experience_for_runtime_award(6, 40).value() == 120);
    assert(scale_experience_for_runtime_award(36, 40).value() == 720);
    assert(scale_experience_for_runtime_award(36, 1).value() == 18);
    assert(scale_experience_for_runtime_award(5, 1).value() == 3);
    assert(scale_experience_for_runtime_award(5, 2).value() == 5);
    assert(!scale_experience_for_runtime_award(-1, 40).has_value());
    assert(!scale_experience_for_runtime_award(36, 999).has_value());
    assert(!scale_experience_for_runtime_award(
            std::numeric_limits<int32_t>::max(), 40).has_value());

    for (const int32_t half_steps : runtime_experience_multiplier_half_steps)
    {
        assert(scale_experience_for_runtime_award(0, half_steps).value() == 0);
        assert(scale_experience_for_runtime_award(1, half_steps).value() == (half_steps + 1) / 2);
        assert(scale_experience_for_runtime_award(999, half_steps).value() == (999 * half_steps + 1) / 2);
    }
    assert(scale_experience_for_runtime_award(
            std::numeric_limits<int32_t>::max(), 2).value() == std::numeric_limits<int32_t>::max());
    return 0;
}
