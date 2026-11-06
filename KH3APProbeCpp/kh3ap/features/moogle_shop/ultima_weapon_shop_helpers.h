#pragma once

#include <cstdint>
#include <string>

namespace RC::Unreal
{
    class UObject;
}

namespace kh3ap
{
    struct UltimaWeaponShopRowCache
    {
        RC::Unreal::UObject* item_data_table{};
        uint64_t row_address{};
        int32_t shop_level_offset{-1};

        auto reset() -> void
        {
            item_data_table = nullptr;
            row_address = 0;
            shop_level_offset = -1;
        }
    };

    struct UltimaWeaponShopLevelUpdateResult
    {
        int previous_shop_level{};
        int desired_shop_level{};
        bool changed{false};
    };

    auto try_update_ultima_weapon_shop_level(
            int desired_shop_level,
            UltimaWeaponShopRowCache& cache,
            UltimaWeaponShopLevelUpdateResult* result,
            std::wstring* failure_details) -> bool;
}
