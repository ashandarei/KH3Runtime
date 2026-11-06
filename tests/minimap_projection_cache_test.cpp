#include "kh3ap/features/ui/map_overlay/minimap_projection_cache.h"
#include <cassert>
#include <limits>
using namespace kh3ap;
int main() {
    MinimapProjectionCache cache;
    int reads=0;
    const MapOverlayMatrix parent{1.2,.15,-.2,.8,17,29};
    std::array<double,3> signature{1920,1080,1};
    auto local=minimap_local_matrix(30,40,1.5,.75,25);
    auto read=[&]() -> std::optional<MinimapIconProjection> {
        ++reads;return MinimapIconProjection{minimap_matrix_product(parent,local),300,300,1500,20,300,300};
    };
    for(int frame=0;frame<10000;++frame) {
        local=minimap_local_matrix(frame*.1,-frame*.05,1.5+frame*.0001,.75,frame*.1);
        const auto result=cache.resolve(local,signature,read);assert(result);
        const auto expected=minimap_matrix_product(parent,local);
        const auto actual=result->canvas.point(12,34), target=expected.point(12,34);
        assert(std::hypot(actual.x-target.x,actual.y-target.y)<1e-8);
    }
    assert(reads==1); // Movement, rotation and zoom retain a live 60 Hz transform.
    signature[2]=2;assert(cache.resolve(local,signature,read));assert(reads==2);
    cache.clear();assert(cache.resolve(local,signature,read));assert(reads==3);
    assert(!cache.resolve(minimap_local_matrix(0,0,0,1,0),signature,read));
    assert(cache.resolve(local,signature,read));assert(reads==4);
    signature[0]=std::numeric_limits<double>::quiet_NaN();
    assert(!cache.resolve(local,signature,read));
    signature[0]=1920;
    assert(!cache.resolve(local,signature,[]{return std::optional<MinimapIconProjection>{};}));
    assert(cache.resolve(local,signature,read));assert(reads==5);
}
