#pragma once

#include "kh3ap/features/world/space/space_ship_catalog.h"
#include <string>

namespace kh3ap::space
{
    inline constexpr int ship_zone_count = static_cast<int>(ship_zone_names.size());
    inline constexpr unsigned all_ship_zones = (1u << ship_zone_count) - 1;
    inline constexpr int completed_world_gameflow = 9999;
    inline constexpr int completed_arendelle_gameflow = 8820;

    inline std::wstring_view zone_blueprint(int zone)
    {
        for (const auto& ship : ship_blueprints)
        {
            if (ship.zone == zone && zone >= 0)
            {
                return ship.name;
            }
        }
        return {};
    }
    inline std::wstring zone_access_guidance(int zone, bool ready, bool received, bool delivered,
                                            unsigned completed_worlds, bool attempted = false)
    {
        if (zone < 0 || zone >= ship_zone_count)
        {
            return {};
        }
        std::wstring text{ship_zone_names[zone]};
        if (!ready)
        {
            return text + L" - Checking space access...";
        }
        const unsigned required = static_cast<unsigned>(zone + 1);
        const bool unlocked = received && delivered && completed_worlds >= required;
        text += unlocked ? L" - AVAILABLE" : attempted ? L" - CANNOT EMBARK" : L" - LOCKED";
        auto blueprint = zone_blueprint(zone);
        constexpr std::wstring_view prefix = L"Gummi Blueprint: ";
        if (blueprint.starts_with(prefix))
        {
            blueprint.remove_prefix(prefix.size());
        }
        text += L"\n";
        text += blueprint;
        text += L" blueprint: ";
        text += received ? L"Acquired" : L"Missing";
        text += L"  |  Main worlds completed: " + std::to_wstring(completed_worlds) + L" / " + std::to_wstring(required);
        return text;
    }
    struct WorldCompletionFlag
    {
        const wchar_t* name;
        int threshold;
    };
    inline constexpr std::array<WorldCompletionFlag, 8> world_completion_flags{{
        {L"gameflow_HE", completed_world_gameflow}, {L"gameflow_TT", completed_world_gameflow},
        {L"gameflow_TS", completed_world_gameflow}, {L"gameflow_RA", completed_world_gameflow},
        {L"gameflow_MI", completed_world_gameflow}, {L"gameflow_FZ", completed_arendelle_gameflow},
        {L"gameflow_CA", completed_world_gameflow}, {L"gameflow_BX", completed_world_gameflow},
    }};
    // A receipt alone cannot open travel before delivery to native inventory.
    template<class OwnsDeliveredShip>
    unsigned unlocked_ship_zones(bool ready, bool enabled, bool inventory_verified,
                                 unsigned completed_worlds, OwnsDeliveredShip owns)
    {
        if (!ready)
        {
            return 0;
        }
        if (!enabled)
        {
            return all_ship_zones;
        }
        if (!inventory_verified)
        {
            return 0;
        }
        unsigned zones{};
        for (const auto& ship : ship_blueprints)
        {
            if (ship.zone >= 0 && ship.zone < ship_zone_count &&
                completed_worlds >= static_cast<unsigned>(ship.zone + 1) && owns(ship.name))
            {
                zones |= 1u << ship.zone;
            }
        }
        return zones;
    }
    constexpr bool ship_zone_available(unsigned zones, int zone = -1)
    {
        return zone == -1 ? zones != 0 : zone >= 0 && zone < ship_zone_count && (zones & (1u << zone)) != 0;
    }
}
