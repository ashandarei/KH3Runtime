#pragma once

#include <cstdint>
#include <limits>
#include <optional>
#include <string_view>

namespace kh3ap
{
    inline constexpr auto lucky_mark_ap_item_category = 2;
    inline constexpr auto lucky_mark_item_category_stride = 256;
    inline constexpr auto lucky_mark_first_ap_item_index = 6;
    inline constexpr auto lucky_mark_last_ap_item_index = 255;

    [[nodiscard]] constexpr auto lucky_mark_native_ap_display_candidate(int32_t item_id) noexcept
        -> bool
    {
        // Seed allocation uses I02200-I02255, then wraps to I02006-I02199.
        return item_id >= lucky_mark_ap_item_category * lucky_mark_item_category_stride +
                              lucky_mark_first_ap_item_index &&
               item_id <= lucky_mark_ap_item_category * lucky_mark_item_category_stride +
                              lucky_mark_last_ap_item_index;
    }

    [[nodiscard]] constexpr auto
    lucky_mark_native_ap_display_row_matches(int32_t item_id,
                                             std::optional<int32_t> ap_key_item_id) noexcept -> bool
    {
        return lucky_mark_native_ap_display_candidate(item_id) && ap_key_item_id.has_value() &&
               *ap_key_item_id == item_id;
    }

    struct LuckyMarkNativeResolution
    {
        int32_t item_id;
        bool process_ap_check;
    };

    [[nodiscard]] constexpr auto
    resolve_lucky_mark_native_ap_reward(int32_t native_item_id,
                                        std::optional<int32_t> ap_item_id) noexcept
        -> LuckyMarkNativeResolution
    {
        // Recognizing an AP token owns transport and milestone retirement even
        // when KH3 already knows its item ID and needs no conversion fallback.
        return {native_item_id == 0 ? ap_item_id.value_or(0) : native_item_id,
                ap_item_id.has_value()};
    }

    inline constexpr int32_t lucky_mark_native_claim_slot_count = 32;
    inline constexpr int32_t lucky_mark_native_retired_alias_index = 0;

    [[nodiscard]] constexpr auto
    lucky_mark_reward_index_needs_retirement(int32_t reward_index) noexcept -> bool
    {
        return reward_index >= lucky_mark_native_claim_slot_count;
    }

    [[nodiscard]] inline auto
    try_parse_lucky_mark_milestone_row_name(std::wstring_view row_name) noexcept
        -> std::optional<int32_t>
    {
        if (row_name.empty())
        {
            return std::nullopt;
        }

        int32_t value = 0;
        for (const wchar_t ch : row_name)
        {
            if (ch < L'0' || ch > L'9')
            {
                return std::nullopt;
            }
            const int32_t digit = static_cast<int32_t>(ch - L'0');
            if (value > (std::numeric_limits<int32_t>::max() - digit) / 10)
            {
                return std::nullopt;
            }
            value = (value * 10) + digit;
        }
        return value > 0 ? std::optional<int32_t>{value} : std::nullopt;
    }

} // namespace kh3ap
