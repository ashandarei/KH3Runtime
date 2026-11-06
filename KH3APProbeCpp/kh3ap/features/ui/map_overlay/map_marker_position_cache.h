#pragma once
#include "kh3ap/features/ui/map_overlay/map_overlay_math.h"
#include <array>
#include <span>
#include <vector>

namespace kh3ap {
// Value-only, bounded by the current live snapshot. Completion/visibility stay
// with the owning tracker; z and Sora-relative height never enter this cache.
class MapMarkerPositionCache {
    struct Entry { double x{},y{}; std::optional<MapOverlayPoint> point; };
    std::vector<Entry> entries_;
    std::array<double,5> context_{};
    bool bound_{};
public:
    void clear() { entries_.clear();bound_=false; }
    template<class Marker,class Project>
    void sync(std::span<const Marker> markers,const MapOverlayArea& area,
              double width,double height,Project&& project) {
        const std::array next{area.scale,area.origin_x,area.origin_y,width,height};
        const bool changed=!bound_||next!=context_;
        const auto old_size=entries_.size();
        entries_.resize(markers.size());
        for(size_t i=0;i<markers.size();++i) {
            const double x=markers[i].location[0],y=markers[i].location[1];
            auto& entry=entries_[i];
            if(changed||i>=old_size||entry.x!=x||entry.y!=y) {
                entry={x,y,project(area,x,y)};
                if(entry.point&&!map_overlay_contains(*entry.point,width,height))entry.point.reset();
            }
        }
        context_=next;bound_=true;
    }
    auto point(size_t index) const -> const std::optional<MapOverlayPoint>& { return entries_[index].point; }
};
}
