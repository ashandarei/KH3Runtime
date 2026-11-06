#pragma once

#include "kh3ap/bridge/catalog/slot_data_lookup_parser.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_set>
#include <vector>

namespace kh3ap
{
    class JsonObject;

    struct UltimaWeaponShopOptions
    {
        bool shop_mode{false};
        bool keyblade_levels_start_at_max{false};
        std::vector<std::wstring> required_keyblades{};
    };

    struct MoogleShopSeedOptions
    {
        int64_t shop_check_count{0};
        bool checks_enabled{false};
        UltimaWeaponShopOptions ultima{};
    };

    struct MoogleShopCheckedLocationsRefresh
    {
        bool updated{false};
        bool changed{false};
    };

    class MoogleShopCatalog
    {
    public:
        auto reset() -> void;
        auto try_load_seed(const std::filesystem::path& slot_data_path, JsonObject& slot_document)
            -> bool;
        auto load_locations(const JsonObject& slot_document, size_t max_locations) -> void;
        auto refresh_checked_locations(const std::filesystem::path& bridge_dir)
            -> MoogleShopCheckedLocationsRefresh;

        auto seed_loaded() const -> bool
        {
            return seed_loaded_;
        }
        auto locations_loaded() const -> bool
        {
            return locations_loaded_;
        }
        auto checked_locations_valid() const -> bool
        {
            return checked_locations_valid_;
        }
        auto options() const -> const MoogleShopSeedOptions&
        {
            return options_;
        }
        auto locations() const -> const std::vector<ShopItemBridgeLocation>&
        {
            return locations_;
        }
        auto checked_bridge_keys() const -> const std::unordered_set<std::wstring>&
        {
            return checked_bridge_keys_;
        }

    private:
        MoogleShopSeedOptions options_{};
        std::vector<ShopItemBridgeLocation> locations_{};
        std::unordered_set<std::wstring> checked_bridge_keys_{};
        uint64_t game_state_revision_{};
        bool seed_loaded_{false};
        bool locations_loaded_{false};
        bool checked_locations_valid_{false};
    };
} // namespace kh3ap
