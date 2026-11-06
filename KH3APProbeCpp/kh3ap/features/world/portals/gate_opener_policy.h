#pragma once
#include <map>
#include <set>
#include <cstdint>
#include <string>
#include "kh3ap/features/world/portals/portal_policy.h"

namespace kh3ap::gate_opener {
struct World { int portal; std::wstring_view name, keyblade; };
inline constexpr std::array worlds{
    World{2, L"Re Mind", L"Kingdom Key"},
    World{3, L"Keyblade Graveyard", L"Starlight"},
    World{4, L"Olympus", L"Hero's Origin"},
    World{5, L"The Caribbean", L"Wheel of Fate"},
    World{6, L"Arendelle", L"Crystal Snow"},
    World{7, L"Kingdom of Corona", L"Ever After"},
    World{8, L"Dark World", L"Oblivion"},
    World{9, L"Radiant Garden", L"Oathkeeper"},
    World{10, L"San Fransokyo", L"Nano Gear"},
    World{11, L"Monstropolis", L"Happy Gear"},
    World{12, L"Toy Box", L"Favorite Deputy"},
    World{13, L"Twilight Town", L"Shooting Star"},
};
inline constexpr int first_portal_id = worlds.front().portal;
inline constexpr int last_portal_id = worlds.back().portal;

inline std::wstring texture(const World& world) {
    constexpr std::wstring_view codes[]{L"KG",L"KG",L"HE",L"CA",L"FZ",L"RA",L"DW",L"RG",L"BX",L"MI",L"TS",L"TT"};
    if (world.portal < first_portal_id || world.portal > last_portal_id)
    {
        return {};
    }
    const auto name = L"MenuWorldTitle" + std::wstring{codes[world.portal - first_portal_id]};
    return L"/Game/UI/Icon/MenuWorldTitle/" + name + L"." + name;
}
// Each choice permanently spends one distinct AP receipt for this seed/player.
// This ledger deliberately survives native save rollback and death retry.
struct State {
    std::map<int64_t, int> spent;
    bool opened(int portal) const {
        if (!portals::by_id(portal)) return false; // Includes the forbidden Portal014.
        for (const auto& [receipt, selected] : spent) if (selected == portal) return true;
        return false;
    }
    size_t available(const std::set<int64_t>& received) const {
        size_t count{};
        for (auto receipt : received) if (!spent.contains(receipt)) ++count;
        return count;
    }
    bool consume(int portal, const std::set<int64_t>& received) {
        if (!portals::by_id(portal) || opened(portal)) return false;
        for (auto receipt : received) if (!spent.contains(receipt)) {
            spent.emplace(receipt, portal);
            return true;
        }
        return false;
    }
};
}
