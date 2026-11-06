#pragma once
#include "kh3ap/features/ui/map_overlay/map_overlay_math.h"
#include <chrono>
#include <cstddef>
#include <cstdint>

namespace kh3ap::map_overlay_settings
{
    inline constexpr auto active_update_interval = std::chrono::milliseconds{16};
    inline constexpr auto idle_update_interval = std::chrono::milliseconds{100};
    inline constexpr auto marker_refresh_interval = std::chrono::milliseconds{500};
    inline constexpr auto layout_probe_interval = std::chrono::milliseconds{250};
    inline constexpr auto layout_readiness_timeout = std::chrono::seconds{5};
    inline constexpr auto diagnostic_audit_interval = std::chrono::seconds{5};
    inline constexpr double slow_update_warning_ms = 4.0;

    // These pools match the authored APMapChestIcons movie.
    inline constexpr int chest_icon_limit = 128;
    inline constexpr int emblem_icon_limit = 90;
    inline constexpr int ingredient_icon_limit = 128;
    inline constexpr size_t emblem_binding_offset = chest_icon_limit;
    inline constexpr size_t ingredient_binding_offset = emblem_binding_offset + emblem_icon_limit;
    inline constexpr size_t minimap_icon_binding_count =
        ingredient_binding_offset + ingredient_icon_limit;
    inline constexpr size_t chest_discovery_limit = 1024;
    inline constexpr int save_point_marker_limit = 64;
    inline constexpr int overlay_child_limit = 128;
    inline constexpr int save_point_marker_type = 0;
    inline constexpr int save_point_marker_layer = 2;
    inline constexpr double marker_height_threshold = 600.0;

    inline constexpr double display_percentage_scale = 100.0;
    inline constexpr double minimum_visible_alpha = 0.01;
    inline constexpr double minimum_icon_scale = 0.6;
    inline constexpr double maximum_icon_scale = 2.0;
    inline constexpr double large_icon_size = 22.0;
    inline constexpr double minimap_icon_size = 16.0;
    inline constexpr double minimap_clip_radius_fraction = 0.75;
    inline constexpr double height_arrow_stroke_width = 2.0;
    inline constexpr uint32_t height_arrow_color = 0xF6CD70;
    inline constexpr double height_arrow_half_width = 3.0;
    inline constexpr double height_arrow_base_offset = 13.0;
    inline constexpr double height_arrow_tip_offset = 16.0;

    inline constexpr double legend_first_row_y = 52.5;
    inline constexpr double legend_row_spacing = 28.0;
    inline constexpr double unowned_legend_alpha = 0.35;
    inline constexpr double map_image_alpha = 0.8;
    inline constexpr uint32_t panel_background_color = 0x081723;
    inline constexpr double panel_background_alpha = 0.28;
    inline constexpr double panel_corner_radius = 12.0;
    inline constexpr double keybind_anchor_x = 0.12;
    inline constexpr double keybind_anchor_y = 0.09;
    inline constexpr double keybind_left_padding = 16.0;
    inline constexpr double battle_level_top_padding = 12.0;
    inline constexpr double battle_level_header_height = 32.0;
    inline constexpr double keybind_header_gap = 6.0;
    inline constexpr double keybind_top_offset =
        battle_level_top_padding + battle_level_header_height + keybind_header_gap;

    inline constexpr double bulk_position_tolerance_pixels = 0.05;
    inline constexpr double bulk_heading_tolerance_degrees = 0.05;
    inline constexpr double projection_audit_tolerance_pixels = 0.08;
    inline constexpr double world_alignment_tolerance_pixels = 3.0;
    inline constexpr double projection_probe_x_fraction = 0.3;
    inline constexpr double projection_probe_y_fraction = 0.7;
    inline constexpr size_t parent_input_reserve =
        166; // Two 16-node transforms plus six stage dimensions.
    inline constexpr size_t frame_input_reserve = 192;
} // namespace kh3ap::map_overlay_settings
