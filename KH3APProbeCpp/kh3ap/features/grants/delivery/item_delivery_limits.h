#pragma once

#include <cstddef>

namespace kh3ap::item_delivery::limits
{
    inline constexpr size_t request_actions = 4096;
    inline constexpr size_t receipt_actions = 40;
    inline constexpr size_t simple_actions_per_frame = 128;
    // A receipt is atomic: the complex frame budget must fit a whole bundle.
    inline constexpr size_t complex_actions_per_frame = receipt_actions;
}
