#pragma once
#include "kh3ap/core/objects/uobject_validity.h"
#include "kh3ap/core/objects/uobject_weak.h"
#include <Unreal/UObject.hpp>
#include <Unreal/UClass.hpp>
#include <utility>
#include <array>
#include <cstdint>
#include <cstring>

namespace kh3ap {
// The UI manager still owns the live array. Retain weak identities and slot
// ordinals only; verify both array entries on every call, rediscover on change.
class LiveUiPartsCache {
    static constexpr int maximum_parts = 128;

public:
    auto resolve(RC::Unreal::UObject* manager, RC::Unreal::UObject* const* parts, int count)
            -> std::pair<RC::Unreal::UObject*, RC::Unreal::UObject*> {
        using namespace RC::Unreal;
        if (!is_uobject_valid_fast(manager) || !parts || count < 0 || count > maximum_parts)
        {
            return {};
        }
        auto at=[&](int index,const FWeakObjectPtr& weak) -> UObject* {
            if (index<0 || index>=count) return nullptr;
            auto* value=weak.Get();
            return value==parts[index] && is_uobject_valid_fast(value)?value:nullptr;
        };
        // Check the complete array identity, including insertions elsewhere.
        // This preserves the original scan's last-matching-part semantics when
        // a newer HUD is appended while an old one is still resident. Stored
        // address bits are compared only; they are never cast or dereferenced.
        if (manager_.Get()==manager && count_==count &&
            std::memcmp(identities_.data(),parts,count*sizeof(UObject*))==0) {
            auto* hud=at(hud_index_,hud_); auto* nav=at(nav_index_,nav_);
            if (hud && nav) return {hud,nav};
        }
        manager_=make_uobject_weak(manager);
        count_=count;
        static_assert(sizeof(uintptr_t)==sizeof(UObject*));
        std::memcpy(identities_.data(),parts,count*sizeof(UObject*));
        hud_={}; nav_={}; hud_index_=nav_index_=-1;
        UObject *hud{}, *nav{};
        for (int i=0;i<count;++i) {
            auto* part=parts[i];
            if (!is_uobject_valid_fast(part)) continue;
            auto* type=part->GetClassPrivate();
            if (!is_uobject_valid_fast(type)) continue;
            const auto name=type->GetName();
            if (name==L"TresUIP_HudRight") { hud=part; hud_index_=i; }
            if (name==L"TresUIP_NaviMapGra") { nav=part; nav_index_=i; }
        }
        hud_=make_uobject_weak(hud); nav_=make_uobject_weak(nav);
        return {hud,nav};
    }
private:
    RC::Unreal::FWeakObjectPtr manager_,hud_,nav_;
    int hud_index_{-1},nav_index_{-1};
    int count_{-1};
    std::array<uintptr_t, maximum_parts> identities_{};
};
}
