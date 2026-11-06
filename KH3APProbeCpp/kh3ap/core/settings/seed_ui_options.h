#pragma once
#include "kh3ap/bridge/serialization/json_fields.h"
#include "kh3ap/features/ui/map_overlay/map_icon_options.h"
#include <array>
#include <cstddef>
#include <utility>

namespace kh3ap {
    namespace seed_ui_limits
    {
        inline constexpr int minimum_battle_offset = -3;
        inline constexpr int maximum_contiguous_battle_offset = 5;
        inline constexpr int bonus_battle_offset = 10;
        inline constexpr int maximum_mom_play_style = 2;
        inline constexpr int maximum_mom_starting_item = 3;
        inline constexpr int maximum_mom_party = 3;
        inline constexpr std::array maximum_mom_values{
            maximum_mom_play_style, maximum_mom_starting_item, maximum_mom_party};
    } // namespace seed_ui_limits

struct RuntimeUiSeedDefaults {
    bool map_overlay_enabled{true};
    bool song_display{true};
    bool auto_equip_movement_abilities{true};
    uint32_t map_icon_options{map_icon_defaults};
    int32_t battle_level_offset{};
    std::array<std::size_t, 4> mom{};
};
inline auto read_runtime_ui_seed_defaults(const JsonObject& slot) -> RuntimeUiSeedDefaults {
    RuntimeUiSeedDefaults result;
    result.map_overlay_enabled = slot.int64_field("map_overlay_enabled").value_or(1) != 0;
    result.song_display = slot.int64_field("song_display").value_or(1) != 0;
    result.auto_equip_movement_abilities = slot.int64_field("auto_equip_movement_abilities").value_or(1) != 0;
    for (const auto& [key, option] : std::array{
            std::pair{"map_chest_icons", MapIconOption::Chests},
            std::pair{"map_emblem_icons", MapIconOption::Emblems},
            std::pair{"map_ingredient_icons", MapIconOption::Ingredients},
            std::pair{"minimap_icons", MapIconOption::Minimap},
            std::pair{"large_map_icons", MapIconOption::LargeMap}}) {
        if (slot.int64_field(key).value_or(1) == 0)
            result.map_icon_options &= ~static_cast<uint32_t>(option);
    }
    if (slot.int64_field("hide_locked_chest_icons").value_or(0) != 0)
        result.map_icon_options &= ~static_cast<uint32_t>(MapIconOption::LockedChests);
    const auto offset = slot.int64_field("battle_level_offset").value_or(0);
    if ((offset >= seed_ui_limits::minimum_battle_offset &&
         offset <= seed_ui_limits::maximum_contiguous_battle_offset) ||
        offset == seed_ui_limits::bonus_battle_offset)
    {
        result.battle_level_offset = static_cast<int32_t>(offset);
    }
    constexpr std::array keys{"melody_of_memory_play_style", "melody_of_memory_starting_item", "melody_of_memory_party"};
    for (std::size_t i = 0; i < keys.size(); ++i) {
        const auto value = slot.int64_field(keys[i]).value_or(0);
        if (value >= 0 && value <= seed_ui_limits::maximum_mom_values[i])
        {
            result.mom[i] = static_cast<std::size_t>(value);
        }
    }
    return result;
}
// Saved menu choices override the seed, including explicit true/zero choices.
inline auto apply_saved_map_ui_options(const JsonObject& saved, RuntimeUiSeedDefaults defaults)
        -> RuntimeUiSeedDefaults {
    defaults.map_overlay_enabled = saved.bool_field("map_overlay_enabled").value_or(defaults.map_overlay_enabled);
    defaults.song_display = saved.bool_field("song_display").value_or(defaults.song_display);
    defaults.auto_equip_movement_abilities = saved.bool_field("auto_equip_movement_abilities").value_or(defaults.auto_equip_movement_abilities);
    for (const auto& [key, option] : std::array{
            std::pair{"map_chest_icons", MapIconOption::Chests},
            std::pair{"map_locked_chest_icons", MapIconOption::LockedChests},
            std::pair{"map_emblem_icons", MapIconOption::Emblems},
            std::pair{"map_ingredient_icons", MapIconOption::Ingredients},
            std::pair{"minimap_icons", MapIconOption::Minimap},
            std::pair{"large_map_icons", MapIconOption::LargeMap}}) {
        const auto bit = static_cast<uint32_t>(option);
        if (const auto enabled = saved.bool_field(key)) {
            if (*enabled) defaults.map_icon_options |= bit;
            else defaults.map_icon_options &= ~bit;
        }
    }
    return defaults;
}
}
