#pragma once

#include "kh3ap/features/world/portals/gate_opener_policy.h"
#include <optional>

namespace kh3ap::portals
{
    inline constexpr int keyblade_mode = 0, party_mode = 1, mixed_mode = 2;

    // World and Keyblade names in the portal catalog are ASCII JSON keys.
    inline auto catalog_key(std::wstring_view name) -> std::string
    {
        std::string key;
        for (wchar_t ch : name)
            key.push_back(static_cast<char>(ch));
        return key;
    }

    // Resolve the seed once. A missing or foreign world's item cannot grant access.
    template <class ReadItem>
    auto party_portal_ids(int64_t mode, ReadItem read_item) -> std::optional<std::set<int>>
    {
        std::set<int> ids;
        if (mode == keyblade_mode)
            return ids;
        if (mode != party_mode && mode != mixed_mode)
            return std::nullopt;
        for (const auto& world : gate_opener::worlds)
        {
            if (mode == mixed_mode)
            {
                const auto item = read_item(catalog_key(world.name));
                if (item == world.keyblade)
                    continue;
                const auto* def = by_id(world.portal);
                if (!def || item != def->first)
                    return std::nullopt;
            }
            ids.insert(world.portal);
        }
        if (mode == mixed_mode && read_item("Data Battles") != read_item("Radiant Garden"))
            return std::nullopt;
        return ids;
    }
}
