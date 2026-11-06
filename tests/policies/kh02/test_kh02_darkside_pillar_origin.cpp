#include "kh3ap/features/kh02/combat/kh02_darkside_pillar_policy.h"
#include <cassert>
#include <limits>

int main()
{
    using kh3ap::kh02_pillar_origin_is_duplicated;
    const std::array<float, 3> forest_origin{4586.087f, -102664.992f, 11275.898f};
    assert(kh02_pillar_origin_is_duplicated(forest_origin, forest_origin, false));
    assert(!kh02_pillar_origin_is_duplicated(forest_origin, forest_origin, true));
    assert(!kh02_pillar_origin_is_duplicated({0, 0, 0}, forest_origin, false));
    assert(!kh02_pillar_origin_is_duplicated({0, 100, 0}, forest_origin, false));
    assert(!kh02_pillar_origin_is_duplicated({0, 0, 0}, {0, 0, 0}, false));
    auto nearby = forest_origin;
    nearby[0] += 0.125f;
    assert(kh02_pillar_origin_is_duplicated(nearby, forest_origin, false));
    nearby[0] += 1.0f;
    assert(!kh02_pillar_origin_is_duplicated(nearby, forest_origin, false));
    nearby = forest_origin;
    nearby[2] = std::numeric_limits<float>::quiet_NaN();
    assert(!kh02_pillar_origin_is_duplicated(nearby, forest_origin, false));
    nearby[2] = std::numeric_limits<float>::infinity();
    assert(!kh02_pillar_origin_is_duplicated(forest_origin, nearby, false));
}
