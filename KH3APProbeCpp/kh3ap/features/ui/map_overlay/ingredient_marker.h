#pragma once
#include <array>

namespace kh3ap {
// Value snapshot; the ingredient service owns the weak actor references.
struct RuntimeIngredientMarker {
    std::array<float,3> location{};
};
}
