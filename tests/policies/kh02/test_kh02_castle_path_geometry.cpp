#include "kh3ap/features/kh02/world/kh02_castle_path_geometry.h"
#include <cassert>

int main()
{
    const kh3ap::Kh02TriggerPlacement road{{100.f, 200.f, 300.f}, {400.f, 500.f, 600.f}, {0.f, 90.f, 0.f}};
    const auto original = kh3ap::kh02_castle_path::placement(road, false);
    assert(original.center == road.center);
    assert(original.extent == road.extent);
    assert(original.rotation == road.rotation);

    const auto expanded = kh3ap::kh02_castle_path::placement(road, true);
    assert(expanded.center[0] == road.center[0] && expanded.center[1] == road.center[1]);
    assert(expanded.extent[0] == road.extent[0] && expanded.extent[1] == road.extent[1]);
    assert(expanded.rotation == road.rotation);
    // Expanding for flight must not move the lower crossing plane or shrink
    // authored horizontal coverage; only the upper plane gains aerial reach.
    assert(expanded.center[2] - expanded.extent[2] == road.center[2] - road.extent[2]);
    assert(expanded.center[2] + expanded.extent[2] == road.center[2] + kh3ap::kh02_castle_path::aerial_upper_reach_cm);
    assert(road.center[2] == 300.f && road.extent[2] == 600.f);

    // Already tall source volumes must retain all their authored coverage.
    auto tall = road;
    tall.extent[2] = 5'000.f;
    const auto unchanged = kh3ap::kh02_castle_path::placement(tall, true);
    assert(unchanged.center == tall.center);
    assert(unchanged.extent == tall.extent);
    assert(unchanged.rotation == tall.rotation);

    auto threshold = road;
    threshold.extent[2] = kh3ap::kh02_castle_path::aerial_upper_reach_cm;
    const auto at_threshold = kh3ap::kh02_castle_path::placement(threshold, true);
    assert(at_threshold.center == threshold.center);
    assert(at_threshold.extent == threshold.extent);
}
