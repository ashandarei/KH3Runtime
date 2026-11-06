#pragma once

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string_view>

namespace kh3ap
{
    inline constexpr uint64_t maximum_battle_sync_input_level = 100;
    inline constexpr int64_t minimum_synced_battle_level = 2;

    inline constexpr int32_t minimum_battle_level_offset = -3;
    inline constexpr int32_t maximum_battle_level_offset = 10;

    [[nodiscard]] inline auto clamp_battle_level_offset(int32_t offset) -> int32_t
    {
        return std::clamp(
                offset,
                minimum_battle_level_offset,
                maximum_battle_level_offset);
    }

    [[nodiscard]] inline auto fixed_battle_level_for_map(
            std::wstring_view context) -> std::optional<uint64_t>
    {
        // The opening Darkside arena always uses level 1, including its
        // streaming gameflow/enemy levels and Re Mind visits to the same map.
        constexpr std::wstring_view map_root = L"/Game/Levels/ew_DLC/ew_04/";
        const auto lower_ascii = [](wchar_t ch) {
            return ch >= L'A' && ch <= L'Z' ? ch + (L'a' - L'A') : ch;
        };
        const auto match = std::search(
                context.begin(), context.end(), map_root.begin(), map_root.end(),
                [lower_ascii](wchar_t left, wchar_t right) {
                    return lower_ascii(left) == lower_ascii(right);
                });
        return match != context.end() ? std::optional<uint64_t>{1} : std::nullopt;
    }

    [[nodiscard]] inline auto resolve_battle_level_target(
            uint64_t sora_level,
            int32_t offset,
            std::wstring_view map_context = {},
            bool sync_enabled = true) -> std::optional<uint64_t>
    {
        if (const auto fixed_level = fixed_battle_level_for_map(map_context))
        {
            return fixed_level;
        }
        if (!sync_enabled || sora_level < 1 || sora_level > maximum_battle_sync_input_level)
        {
            return std::nullopt;
        }

        const int64_t target = static_cast<int64_t>(sora_level) +
                static_cast<int64_t>(clamp_battle_level_offset(offset));
        return static_cast<uint64_t>(std::max<int64_t>(minimum_synced_battle_level, target));
    }
}
