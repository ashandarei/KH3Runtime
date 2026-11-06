#include "kh3ap/features/world/space/space_ship_policy.h"
#include "kh3ap/features/world/space/space_policy.h"
#include <cassert>
#include <algorithm>
#include <set>
#include <string>
using namespace kh3ap::space;
int main() {
    std::array<unsigned, 3> counts{};
    std::set<std::wstring_view> names;
    for (const auto& ship : ship_blueprints) {
        assert(ship.zone >= -1 && ship.zone < 3);
        if (ship.zone >= 0) ++counts[ship.zone];
        assert(names.insert(ship.name).second);
        const auto only = [&](std::wstring_view name) { return name == ship.name; };
        const auto expected = ship.zone >= 0 ? 1u << ship.zone : 0u;
        assert(unlocked_ship_zones(true, true, true, 8, only) == expected);
        assert(unlocked_ship_zones(false, true, true, 8, only) == 0);
        assert(unlocked_ship_zones(true, true, false, 8, only) == 0);
        assert(unlocked_ship_zones(true, false, false, 0, only) == 7);
        for (int zone = 0; zone < 3; ++zone)
            assert(ship_zone_available(expected, zone) == (zone == ship.zone));
    }
    assert((counts == std::array<unsigned, 3>{1,1,1}));
    const std::array<std::wstring_view, 3> zone_ships{
        L"Gummi Blueprint: Bomb", L"Gummi Blueprint: Galleon", L"Gummi Blueprint: Submarine"};
    for (unsigned zone = 0; zone < zone_ships.size(); ++zone) {
        const auto only = [&](std::wstring_view name) { return name == zone_ships[zone]; };
        assert(unlocked_ship_zones(true, true, true, 8, only) == (1u << zone));
    }
    for (unsigned completed = 0; completed <= 8; ++completed) {
        const auto all_ships = [](std::wstring_view) { return true; };
        const unsigned expected = completed >= 3 ? 7 : (1u << completed) - 1;
        assert(unlocked_ship_zones(true, true, true, completed, all_ships) == expected);
        for (unsigned zone = 0; zone < zone_ships.size(); ++zone) {
            const auto only = [&](std::wstring_view name) { return name == zone_ships[zone]; };
            assert(unlocked_ship_zones(true, true, true, completed, only) ==
                   (completed > zone ? 1u << zone : 0));
        }
    }
    assert(names.size() == 37);
    const auto only_other_ships = [](std::wstring_view name) {
        return std::any_of(ship_blueprints.begin(), ship_blueprints.end(),
            [&](const auto& ship) { return ship.zone == -1 && ship.name == name; });
    };
    assert(unlocked_ship_zones(true, true, true, 8, only_other_ships) == 0);
    assert(!names.contains(L"Gummi Blueprint: Highwind LV 1"));
    assert(std::none_of(ship_blueprints.begin(), ship_blueprints.end(),
        [](const auto& ship) { return ship.token == L"GUMISHIPBP_ITEM056"; }));
    const auto native_highwind = [](std::wstring_view name) {
        return name == L"Gummi Blueprint: Highwind LV 1";
    };
    assert(unlocked_ship_zones(true, true, true, 8, native_highwind) == 0);
    std::set<std::wstring_view> received, delivered;
    const auto owns = [&](std::wstring_view name) { return received.contains(name) && delivered.contains(name); };
    assert(unlocked_ship_zones(true, true, true, 8, owns) == 0);
    for (unsigned zone = 0; zone < 3; ++zone) {
        const auto ship = std::find_if(ship_blueprints.begin(), ship_blueprints.end(),
            [&](const auto& row) { return row.zone == static_cast<int>(zone); })->name;
        received.insert(ship);
        assert(unlocked_ship_zones(true, true, true, 8, owns) == (1u << zone)-1);
        delivered.insert(ship);
        assert(unlocked_ship_zones(true, true, true, 8, owns) == (1u << (zone+1))-1);
    }
    assert(zone_access_guidance(-1, true, false, false, 0).empty());
    assert(zone_access_guidance(3, true, false, false, 0).empty());
    assert(zone_access_guidance(0, false, false, false, 0) == L"Starlight Way - Checking space access...");
    // Ownership follows the AP receipt, while travel still requires native stock.
    const auto received_not_delivered = zone_access_guidance(1, true, true, false, 2);
    assert(received_not_delivered.find(L"Galleon blueprint: Acquired") != std::wstring::npos);
    assert(received_not_delivered.find(L"LOCKED") != std::wstring::npos);
    assert(received_not_delivered.find(L"Awaiting delivery") == std::wstring::npos);
    for (int zone = 0; zone < 3; ++zone) for (unsigned clears = 0; clears <= 8; ++clears)
        for (bool received : {false, true}) for (bool delivered : {false, true}) {
            const auto text = zone_access_guidance(zone, true, received, delivered, clears);
            const bool allowed = received && delivered && clears >= static_cast<unsigned>(zone + 1);
            assert((text.find(L"AVAILABLE") != text.npos) == allowed);
            assert((text.find(L"LOCKED") != text.npos) == !allowed);
            const auto attempt = zone_access_guidance(zone, true, received, delivered, clears, true);
            assert((attempt.find(L"CANNOT EMBARK") != attempt.npos) == !allowed);
        }
    for (int zone : {-2,3,99}) assert(!ship_zone_available(7,zone));
    assert(!ship_zone_available(0));
    assert(ship_zone_available(1));
    for (int zone = 0; zone < 3; ++zone) {
        assert(flight_zone(travel_maps[zone]) == zone);
        assert(flight_zone(L"World " + std::wstring{travel_maps[zone]} + L".gm_0" + std::to_wstring(zone+1)) == zone);
    }
    assert(flight_zone(world_map) == -1);
    assert(flight_zone(L"/Game/Levels/gm/gm_04/gm_04") == -1);
    assert(flight_zone(L"/Game/Levels/gm/gm_01_extra/gm_01") == -1);
    for (unsigned i = 0; i < world_symbol_codes.size(); ++i)
        assert(landing_zone(world_symbol_codes[i]) == (i < 4 ? 0 : i < 8 ? 1 : 2));
    assert(landing_zone(L"dw") == -1);
}
