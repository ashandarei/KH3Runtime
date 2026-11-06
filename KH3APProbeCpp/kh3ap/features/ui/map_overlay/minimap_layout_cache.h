#pragma once
#include "kh3ap/core/objects/uobject_validity.h"
#include "kh3ap/core/objects/uobject_weak.h"
#include <Unreal/FWeakObjectPtr.hpp>
#include <array>
#include <cmath>
#include <optional>

namespace kh3ap {
// Cache only stable bindings and dimensions. Bounds, matrices, visibility and
// player geometry remain live. A missing viewport disables dimension reuse.
class MinimapLayoutCache {
public:
    using Object=RC::Unreal::UObject;
    using Owners=std::array<Object*,5>; // HUD root, map root/canvas, texture, icon root
    using Viewport=std::array<int,2>;
    enum Binding { Bitmap, NativeStage, HudStage, IconStage, BindingCount };
    void clear() { *this={}; }
    bool sync(const Owners& owners,const std::optional<Viewport>& viewport) {
        bool same=bound_&&viewport&&viewport==viewport_;
        for(size_t i=0;i<owners.size();++i)
            same=same&&is_uobject_valid_fast(owners[i])&&owners_[i].Get()==owners[i];
        if(same)return false;
        clear();
        if(!viewport||(*viewport)[0]<=0||(*viewport)[1]<=0)return true;
        for(size_t i=0;i<owners.size();++i) {
            owners_[i]=make_uobject_weak(owners[i]);
            if(!owners[i]||owners_[i].Get()!=owners[i]){clear();return true;}
        }
        viewport_=viewport;bound_=true;return true;
    }
    template<class Resolve> Object* object(Binding slot,Resolve&& resolve) {
        if(bound_)if(auto* value=objects_[slot].Get();is_uobject_valid_fast(value))return value;
        auto* value=resolve();
        // A collected/replaced wrapper invalidates values derived from it.
        image_.reset();stages_.reset();
        if(bound_)objects_[slot]=make_uobject_weak(value);
        return is_uobject_valid_fast(value)?value:nullptr;
    }
    template<class Read> auto image_size(Read&& read)->std::optional<std::array<double,2>> {
        return sizes(image_,read);
    }
    template<class Read> auto stage_sizes(Read&& read)->std::optional<std::array<double,6>> {
        return sizes(stages_,read);
    }
private:
    template<size_t N,class Read> auto sizes(std::optional<std::array<double,N>>& cached,Read& read)
            ->std::optional<std::array<double,N>> {
        if(bound_&&cached)return cached;
        auto values=read();
        for(double value:values)if(!std::isfinite(value)||value<=0)return {};
        if(bound_)cached=values;
        return values;
    }
    bool bound_{};
    std::optional<Viewport> viewport_;
    std::array<RC::Unreal::FWeakObjectPtr,5> owners_{};
    std::array<RC::Unreal::FWeakObjectPtr,BindingCount> objects_{};
    std::optional<std::array<double,2>> image_;
    std::optional<std::array<double,6>> stages_;
};
}
