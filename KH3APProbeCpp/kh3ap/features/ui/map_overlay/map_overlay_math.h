#pragma once

#include <algorithm>
#include <cmath>
#include <chrono>
#include <optional>
#include <set>
#include <utility>

namespace kh3ap
{
    namespace map_overlay_settings
    {
        inline constexpr double reference_width = 1920.0;
        inline constexpr double reference_height = 1080.0;
        inline constexpr double viewport_left_fraction = 0.09;
        inline constexpr double viewport_width_fraction = 0.82;
        inline constexpr double viewport_height_fraction = 0.90;
        inline constexpr double legend_right_fraction = 0.86;
        inline constexpr double legend_top_fraction = 0.09;
        inline constexpr double legend_height_fraction = 0.82;
        inline constexpr double legend_top_padding = 56.0;
        inline constexpr double legend_bottom_padding = 72.0;
        inline constexpr double minimum_zoom = 0.5;
        inline constexpr double maximum_zoom = 2.0;
        inline constexpr double zoom_step = 0.25;
        inline constexpr double chest_zoom_weight = 0.5;
        inline constexpr double scale_tolerance = 0.0001;
        inline constexpr double pixel_tolerance = 1.0;
        inline constexpr double maximum_world_scale = 10.0;
        inline constexpr double minimum_transform_scale = 0.00001;
        inline constexpr double degrees_to_radians = 0.017453292519943295;
        inline constexpr int pressed_event = 0;
        inline constexpr int released_event = 1;
        inline constexpr auto controller_hold_delay = std::chrono::milliseconds{250};
    } // namespace map_overlay_settings
    struct MapOverlayPoint
    {
        double x{};
        double y{};
        double heading{};
    };
    struct MapOverlayArea
    {
        double scale{};
        double origin_x{};
        double origin_y{};
    };
    struct MapOverlayLayout
    {
        double fit{};
        double map_x{};
        double map_y{};
        double legend_x{};
        double legend_y{};
        double legend_scale{};
        double view_x{};
        double view_y{};
        double view_width{};
        double view_height{};
    };
    // Scaleform rounds scroll rectangles to pixels and display positions to twips.
    // A scale is dimensionless: applying pixel tolerance there would hide real drift.
    inline auto map_overlay_layout_matches(double actual, double expected, bool scale = false)
        -> bool
    {
        return std::isfinite(actual) && std::isfinite(expected) &&
               std::abs(actual - expected) <= (scale ? map_overlay_settings::scale_tolerance
                                                     : map_overlay_settings::pixel_tolerance);
    }
    inline auto map_overlay_layout(double stage_width, double stage_height, double image_width,
                                   double image_height, double legend_width, double legend_height,
                                   double zoom = 1, std::optional<MapOverlayPoint> focus = {})
        -> MapOverlayLayout
    {
        using namespace map_overlay_settings;
        const double stage_scale =
            std::min(stage_width / reference_width, stage_height / reference_height);
        // The legend floats above the map on the right; it never reserves space
        // or changes the centered viewport used for fitting and following Sora.
        const double left = stage_width * viewport_left_fraction;
        const double available_width = stage_width * viewport_width_fraction;
        const double available_height = stage_height * viewport_height_fraction;
        const double top = (stage_height - available_height) / 2;
        const double fit =
            std::min(available_width / image_width, available_height / image_height) *
            std::clamp(zoom, minimum_zoom, maximum_zoom);
        // At overview scale center the whole texture. Once it exceeds the viewport,
        // follow Sora, clamping at the texture edges rather than exposing blank space.
        auto origin = [](double start, double viewport, double extent, double target)
        {
            return extent <= viewport ? start + (viewport - extent) / 2
                                      : std::clamp(start + viewport / 2 - target,
                                                   start + viewport - extent, start);
        };
        return {fit,
                origin(left, available_width, image_width * fit,
                       (focus ? focus->x : image_width / 2) * fit),
                origin(top, available_height, image_height * fit,
                       (focus ? focus->y : image_height / 2) * fit),
                stage_width * legend_right_fraction - legend_width * stage_scale,
                stage_height * legend_top_fraction + legend_top_padding * stage_scale,
                std::min(stage_scale, (stage_height * legend_height_fraction -
                                       legend_bottom_padding * stage_scale) /
                                          legend_height),
                left,
                top,
                available_width,
                available_height};
    }

    class MapOverlayZoom
    {
        static constexpr int default_step = static_cast<int>(1.0 / map_overlay_settings::zoom_step);
        static constexpr int minimum_step =
            static_cast<int>(map_overlay_settings::minimum_zoom / map_overlay_settings::zoom_step);
        static constexpr int maximum_step =
            static_cast<int>(map_overlay_settings::maximum_zoom / map_overlay_settings::zoom_step);
        std::set<std::pair<int, int>> held_;
        int step_{default_step};

    public:
        auto factor() const -> double
        {
            return step_ * map_overlay_settings::zoom_step;
        }
        // Preserve normal size at 100%; each 25% map step changes chests by 12.5%.
        auto chest_factor() const -> double
        {
            return 1 + (factor() - 1) * map_overlay_settings::chest_zoom_weight;
        }
        auto route(int controller, int direction, int event, bool opened, bool& changed) -> bool
        {
            changed = false;
            if (direction != -1 && direction != 1)
            {
                return false;
            }
            const auto key = std::pair{controller, direction};
            // Consume a captured trigger's release even if the overlay/menu closes.
            if (held_.contains(key))
            {
                if (event == map_overlay_settings::released_event)
                {
                    held_.erase(key);
                }
                return true;
            }
            if (!opened || event != map_overlay_settings::pressed_event)
            {
                return false;
            }
            held_.insert(key);
            const int next_step = std::clamp(step_ + direction, minimum_step, maximum_step);
            changed = next_step != step_;
            step_ = next_step;
            return true;
        }
    };
    struct MapOverlayMatrix
    {
        double a{1};
        double b{};
        double c{};
        double d{1};
        double tx{};
        double ty{};
        auto point(double x, double y) const -> MapOverlayPoint
        {
            return {a * x + c * y + tx, b * x + d * y + ty, 0};
        }
    };
    struct MinimapIconProjection
    {
        MapOverlayMatrix canvas;
        double source_width{};
        double source_height{};
        double x{};
        double y{};
        double width{};
        double height{};
        auto project(double point_x, double point_y, double inset) const
            -> std::optional<MapOverlayPoint>
        {
            for (double value :
                 {canvas.a, canvas.b, canvas.c, canvas.d, canvas.tx, canvas.ty, source_width,
                  source_height, x, y, width, height, point_x, point_y, inset})
            {
                if (!std::isfinite(value))
                {
                    return {};
                }
            }
            if (source_width <= 0 || source_height <= 0 || width <= 2 * inset ||
                height <= 2 * inset)
            {
                return {};
            }
            const auto canvas_point = canvas.point(point_x, point_y);
            MapOverlayPoint screen{x + canvas_point.x * width / source_width,
                                   y + canvas_point.y * height / source_height, 0};
            // Leave a full icon inside the native minimap's visible rectangle.
            if (screen.x < x + inset || screen.y < y + inset || screen.x > x + width - inset ||
                screen.y > y + height - inset)
            {
                return {};
            }
            return screen;
        }
    };
    inline auto map_overlay_world_point(const MapOverlayArea& area, double x, double y)
        -> std::optional<MapOverlayPoint>
    {
        for (double value : {area.scale, area.origin_x, area.origin_y, x, y})
        {
            if (!std::isfinite(value))
            {
                return {};
            }
        }
        if (area.scale <= 0 || area.scale > map_overlay_settings::maximum_world_scale)
        {
            return {};
        }
        return MapOverlayPoint{(x - area.origin_x) * area.scale, (y - area.origin_y) * area.scale,
                               0};
    }
    inline auto map_overlay_contains(const MapOverlayPoint& point, double width, double height)
        -> bool
    {
        return std::isfinite(point.x) && std::isfinite(point.y) && point.x >= 0 && point.y >= 0 &&
               point.x < width && point.y < height;
    }
    // Native landmark markers live beneath a layer inside the map canvas.
    inline auto map_overlay_canvas_point(double point_x, double point_y, double x, double y,
                                         double scale_x, double scale_y, double rotation)
        -> std::optional<MapOverlayPoint>
    {
        for (double value : {point_x, point_y, x, y, scale_x, scale_y, rotation})
        {
            if (!std::isfinite(value))
            {
                return {};
            }
        }
        if (std::abs(scale_x) < map_overlay_settings::minimum_transform_scale ||
            std::abs(scale_y) < map_overlay_settings::minimum_transform_scale)
        {
            return {};
        }
        const double radians = rotation * map_overlay_settings::degrees_to_radians;
        const double cosine = std::cos(radians);
        const double sine = std::sin(radians);
        return MapOverlayPoint{x + point_x * scale_x * cosine - point_y * scale_y * sine,
                               y + point_x * scale_x * sine + point_y * scale_y * cosine, rotation};
    }
    // Invert the game's live map-canvas transform. The PC marker is its sibling;
    // both coordinates are therefore in the same parent space, independent of
    // camera rotation, the outer map transform, HUD scale, or world-map metadata.
    inline auto map_overlay_point(double point_x, double point_y, double heading, double x,
                                  double y, double scale_x, double scale_y, double rotation)
        -> std::optional<MapOverlayPoint>
    {
        for (double value : {point_x, point_y, heading, x, y, scale_x, scale_y, rotation})
        {
            if (!std::isfinite(value))
            {
                return {};
            }
        }
        if (std::abs(scale_x) < map_overlay_settings::minimum_transform_scale ||
            std::abs(scale_y) < map_overlay_settings::minimum_transform_scale)
        {
            return {};
        }
        const double radians = rotation * map_overlay_settings::degrees_to_radians;
        const double cosine = std::cos(radians);
        const double sine = std::sin(radians);
        return MapOverlayPoint{((point_x - x) * cosine + (point_y - y) * sine) / scale_x,
                               (-(point_x - x) * sine + (point_y - y) * cosine) / scale_y,
                               heading - rotation};
    }

    class MapOverlayInput
    {
        using Clock = std::chrono::steady_clock;
        std::set<std::pair<int, bool>> held_;
        struct Hold
        {
            std::pair<int, bool> key;
            Clock::time_point due;
        };
        std::optional<Hold> opening_;

    public:
        auto pending() const -> bool
        {
            return opening_.has_value();
        }
        auto cancel_open() -> void
        {
            opening_.reset();
        }
        auto advance(Clock::time_point now, bool eligible, bool opened) -> bool
        {
            if (!eligible || opened)
            {
                cancel_open();
                return false;
            }
            if (!opening_ || now < opening_->due)
            {
                return false;
            }
            cancel_open();
            return true;
        }
        // Track the entire owned key cycle, including release after a menu opens.
        auto route(int controller, bool keyboard, int event, bool eligible, bool opened,
                   Clock::time_point now, bool& toggle) -> bool
        {
            toggle = false;
            const auto key = std::pair{controller, keyboard};
            if (!eligible)
            {
                cancel_open();
            }
            if (held_.contains(key))
            {
                if (event == map_overlay_settings::released_event)
                {
                    if (opening_ && opening_->key == key)
                    {
                        toggle = advance(now, eligible, opened);
                        cancel_open();
                    }
                    held_.erase(key);
                }
                return true;
            }
            if (event != map_overlay_settings::pressed_event || !eligible)
            {
                return false;
            }
            held_.insert(key);
            if (!keyboard && !opened)
            {
                if (!opening_)
                {
                    opening_ = Hold{key, now + map_overlay_settings::controller_hold_delay};
                }
            }
            else
            {
                cancel_open();
                toggle = true;
            }
            return true;
        }
    };
} // namespace kh3ap
