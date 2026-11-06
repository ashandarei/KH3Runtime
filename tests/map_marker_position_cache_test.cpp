#include "kh3ap/features/ui/map_overlay/map_marker_position_cache.h"
#include "kh3ap/features/ui/map_overlay/ingredient_marker.h"
#include <cassert>
#include <limits>
using namespace kh3ap;
int main() {
    MapMarkerPositionCache cache;
    MapOverlayArea area{1,0,0};
    std::vector<RuntimeIngredientMarker> live{{{10,20,0}},{{30,40,0}}};
    int projections=0;
    auto sync=[&](double width=100) { cache.sync(std::span<const RuntimeIngredientMarker>{live},area,width,100,
        [&](const auto& a,double x,double y) { ++projections;return map_overlay_world_point(a,x,y); }); };
    sync();assert(projections==2&&cache.point(0)->x==10);
    for(int i=0;i<10000;++i) { live[0].location[2]=float(i);sync(); }
    assert(projections==2); // Live height changes do not redo the map position.
    live[1].location[0]=150;sync();assert(projections==3&&!cache.point(1));
    sync();assert(projections==3); // Off-map positions are cached too.
    live.erase(live.begin());sync();assert(projections==4&&!cache.point(0));
    live.clear();sync();live.push_back({{30,40,0}});sync();
    assert(projections==5&&cache.point(0)->x==30); // Hidden/completed roster entries disappear immediately.
    area.origin_x=20;sync();assert(projections==6&&cache.point(0)->x==10);
    sync(5);assert(projections==7&&!cache.point(0)); // Texture bounds invalidate clipping.
    area.scale=2;sync();assert(projections==8&&cache.point(0)->x==20);
    live[0].location[0]=std::numeric_limits<float>::quiet_NaN();sync();assert(!cache.point(0));
    live[0].location[0]=30;sync();assert(cache.point(0));
    cache.clear();sync();assert(projections==11);
}
