#pragma once
#include "keyblade_level_limits.h"

#include <string>
#include <string_view>
#include <vector>
#include <array>
#include <cstdint>

namespace kh3ap
{
    struct NativeWorkshopSnapshot
    {
        uint64_t save_data{};
        std::array<uint32_t, 8> synthesis{};
        std::array<uint32_t, 8> photos{};
        std::array<uint32_t, 8> enhancements{};
    };
    auto try_query_native_workshop(NativeWorkshopSnapshot& result, std::wstring* failure_details) -> bool;
    struct ProgressiveKeybladeLevelApplyResult
    {
        int target_level{};
        int expected_flag_count{};
        int already_set_count{};
        int newly_set_count{};
        bool applies_to_all_keyblades{true};
        std::wstring keyblade_name{};
        bool item_manager_refreshed{false};
    };

    struct UltimaWeaponShopPrerequisiteQueryResult
    {
        int maxed_flag_count{};
        int required_flag_count{};
        bool all_required_keyblades_maxed{false};
    };

    struct KeybladeLevelQueryResult
    {
        std::wstring keyblade_name{};
        int current_level{};
        int maximum_level{keyblade_levels::maximum};
        int achieved_flag_count{};
    };

    auto try_query_keyblade_level(
            std::wstring_view keyblade_name,
            bool levels_start_at_max,
            KeybladeLevelQueryResult* result,
            std::wstring* failure_details) -> bool;

    auto try_query_ultima_weapon_shop_prerequisites(
            const std::vector<std::wstring>& required_keyblade_names,
            UltimaWeaponShopPrerequisiteQueryResult* result,
            std::wstring* failure_details) -> bool;

    auto try_apply_progressive_keyblade_level(
            std::wstring_view runtime_token,
            std::wstring_view preferred_item_manager_path,
            bool force_item_manager_refresh,
            ProgressiveKeybladeLevelApplyResult* result,
            std::wstring* failure_details) -> bool;
}
