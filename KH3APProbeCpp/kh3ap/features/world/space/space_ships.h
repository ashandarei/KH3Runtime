#pragma once
#include "kh3ap/features/world/space/space_access.h"
#include <Unreal/UObject.hpp>
#include <iosfwd>
#include <map>

namespace kh3ap {
    struct RuntimeSpaceShips {
        bool inspect(RC::Unreal::UObject* context);
        bool reconcile(RC::Unreal::UObject* context, const RuntimeSpaceAccess& access);
        unsigned unlocked_zones(const RuntimeSpaceAccess& access) const;
        bool usable(const RuntimeSpaceAccess& access, int zone = -1) const;
        void write_status(std::ostream& json, const RuntimeSpaceAccess& access) const;
        std::map<std::wstring, int> stock;
        bool inventory_verified{};
        unsigned completed_worlds{};
        uint64_t removed{};
    };
}
