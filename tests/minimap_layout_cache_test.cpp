#include "kh3ap/features/ui/map_overlay/minimap_layout_cache.h"
#include "kh3ap/features/ui/map_overlay/minimap_ancestry_cache.h"
#include <Unreal/Runtime.hpp>
#include <cassert>
#include <limits>
using namespace kh3ap;
using namespace RC::Unreal;
auto kh3ap::make_uobject_weak(UObject* object)->FWeakObjectPtr {FWeakObjectPtr weak;if(is_uobject_valid_fast(object))weak=object;return weak;}
int main() {
    UClass type;type.index=0;type.type=&type;UObjectArray::items.push_back({&type,true,1});
    UObject hud,map,canvas,texture,icons,bitmap,stage;
    for(auto* p:{&hud,&map,&canvas,&texture,&icons,&bitmap,&stage}) {
        p->type=&type;p->index=UObjectArray::GetNumElements();UObjectArray::items.push_back({p,true,1});
    }
    MinimapLayoutCache cache;
    const MinimapLayoutCache::Owners owners{&hud,&map,&canvas,&texture,&icons};
    int object_reads=0,image_reads=0,stage_reads=0;
    double view_w=1920,view_h=1080;
    auto bind=[&]{return cache.object(MinimapLayoutCache::Bitmap,[&]{++object_reads;return &bitmap;});};
    auto image=[&]{return cache.image_size([&]{++image_reads;return std::array{300.,300.};});};
    auto stages=[&]{return cache.stage_sizes([&]{++stage_reads;return std::array{view_w,view_h,view_w,view_h,view_w,view_h};});};
    for(int frame=0;frame<10000;++frame) {
        cache.sync(owners,MinimapLayoutCache::Viewport{1920,1080});
        assert(bind()==&bitmap&&image()&&stages());
    }
    assert(object_reads==1&&image_reads==1&&stage_reads==1);
    view_w=1280;view_h=720;
    cache.sync(owners,MinimapLayoutCache::Viewport{1280,720});
    assert(bind()&&image()&&stages()&&object_reads==2&&stage_reads==2); // resize
    assert((*stages())[0]==1280&&(*stages())[1]==720&&stage_reads==2);
    ++UObjectArray::items[hud.index].serial; // Same address, rebuilt HUD identity.
    cache.sync(owners,MinimapLayoutCache::Viewport{1280,720});
    assert(bind()&&image()&&stages()&&object_reads==3&&stage_reads==3);
    ++UObjectArray::items[bitmap.index].serial; // A cached wrapper was collected/replaced.
    assert(bind()&&image()&&stages()&&object_reads==4&&stage_reads==4);
    cache.clear(); // menu, texture or map transition
    cache.sync(owners,MinimapLayoutCache::Viewport{1280,720});assert(bind());
    assert(!cache.image_size([]{return std::array{0.,300.};})); // async bitmap not ready
    assert(!cache.image_size([]{return std::array{std::numeric_limits<double>::quiet_NaN(),300.};}));
    assert(image()&&stages()&&stage_reads==5); // Failed reads never poison the cache.
    for(int frame=0;frame<2;++frame) {
        cache.sync(owners,std::nullopt);assert(bind()&&image()&&stages());
    }
    assert(stage_reads==7); // Unknown viewport always reads live.
    auto absent=owners;absent[3]=nullptr;cache.sync(absent,MinimapLayoutCache::Viewport{1280,720});
    assert(bind()&&image()&&stages()&&stage_reads==8);
    MinimapAncestryCache ancestry;
    const auto now=MinimapAncestryCache::Clock::now();
    int parent_reads=0,visibility_reads=0;
    bool reparented=false,shown=true;
    auto parent=[&](UObject* p)->UObject* {++parent_reads;return p==&bitmap&&!reparented?&stage:nullptr;};
    assert(ancestry.sync(&bitmap,&hud,now,parent));
    assert(parent_reads==2);
    for(int i=1;i<6;++i) {
        assert(!ancestry.sync(&bitmap,&hud,now+std::chrono::milliseconds(i*16),parent));
        assert(ancestry.visit([&](UObject*){++visibility_reads;return shown;}));
    }
    assert(parent_reads==2&&visibility_reads==10);
    shown=false;assert(!ancestry.visit([&](UObject*){return shown;})); // Immediate hide.
    shown=true;reparented=true;
    assert(ancestry.sync(&bitmap,&hud,now+std::chrono::milliseconds(100),parent));
    visibility_reads=0;assert(ancestry.visit([&](UObject*){++visibility_reads;return true;}));
    assert(visibility_reads==1); // AS reparent without an owning movie replacement.
    ++UObjectArray::items[bitmap.index].serial;
    assert(ancestry.sync(&bitmap,&hud,now+std::chrono::milliseconds(101),parent));
    ++UObjectArray::items[hud.index].serial;
    assert(ancestry.sync(&bitmap,&hud,now+std::chrono::milliseconds(102),parent));
    assert(ancestry.sync(nullptr,&hud,now+std::chrono::milliseconds(103),parent));
    assert(!ancestry.visit([](UObject*){return true;}));
}
