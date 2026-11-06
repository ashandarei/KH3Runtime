#include "kh3ap/features/ui/map_overlay/service_impl.h"
#include "kh3ap/features/ui/map_overlay/timing.h"
#include "kh3ap/features/ui/map_overlay/map_chest_movie.h"
#include <DynamicOutput/DynamicOutput.hpp>
#include <algorithm>
#include <cmath>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

namespace kh3ap
{
    using namespace RC;
    using namespace RC::Unreal;
    using namespace map_overlay_detail;
    using namespace map_overlay_settings;

    auto RuntimeMapOverlayService::Impl::tick() -> void
    {

        const auto options = icon_options();
        if (last_icon_options != options)
        {
            last_icon_options = options;
            next_chests = {};
            next_minimap_markers = {};
            chest_layout_dirty = true;
        }
        const bool mini_enabled =
            map_icon_enabled(options, MapIconOption::Minimap) && (options & map_marker_options);
        if (!enabled())
        {
            if (opened)
            {
                suspend();
            }
            eligible = false;
            pending_toggle = false;
            input.cancel_open();
        }
        if (!opened && !pending_toggle && movie_path.empty() && !input.pending() && !mini_enabled &&
            chest_movie_path.empty() && minimap_movie_path.empty())
        {
            return;
        }
        const auto now = std::chrono::steady_clock::now();
        // Only an active L3 hold needs this deadline check. No timer or HUD
        // reflection is added to the unused-overlay path.
        if (input.pending())
        {
            auto* p = config.pawn ? config.pawn() : nullptr;
            const bool ready = p && hold_pawn.Get() == p && is_uobject_valid_fast(p) &&
                               config.gameplay_ready && config.gameplay_ready();
            if (input.advance(now, ready, opened))
            {
                pending_toggle = true;
            }
            if (!opened && !pending_toggle && movie_path.empty() && !mini_enabled)
            {
                return;
            }
        }
        if (now < next_tick && !pending_toggle)
        {
            return;
        }
        next_tick = now + (opened || mini_enabled ? active_update_interval : idle_update_interval);
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
        MapTickTimer timing;
        timing.report = pending_toggle;
#endif
        auto* p = config.pawn ? config.pawn() : nullptr;
        const bool ready =
            is_uobject_valid_fast(p) && config.gameplay_ready && config.gameplay_ready();
        if (!ready ||
            ((!movie_path.empty() || !chest_movie_path.empty() || !minimap_movie_path.empty()) &&
             pawn.Get() != p))
        {
            hide(nullptr);
            eligible = false;
            pending_toggle = false;
            if (!ready)
            {
                return;
            }
        }
        pawn = p;
        // The normal minimap and its separate AP icons are hidden only while
        // the large map is presented. Closing restores the original opacity.
        if (presented)
        {
            suppress_native_minimap(p);
        }
        else
        {
            restore_native_minimap();
            tick_minimap(p, now);
        }
        if (!opened && !pending_toggle)
        {
            return;
        }
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
        RuntimeHotPathProfileScope map_profile{L"on_update:map_overlay"};
#endif
        // Native HUD/roots are resolved live; mod movie identities use the
        // verified native serial allocator instead of plain weak assignment.
        auto [h, n] = live_parts(p);
        auto* r = root(n);
        auto* texture = object(n, L"m_LoadedAsyncTexture");
        auto* asset = object(n, L"m_SwfMovie");
        eligible = r && texture && asset;
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
        constexpr int hud_available_flag = 1 << 0;
        constexpr int nav_available_flag = 1 << 1;
        constexpr int root_available_flag = 1 << 2;
        constexpr int texture_available_flag = 1 << 3;
        constexpr int eligible_flag = 1 << 4;
        const int status = (h ? hud_available_flag : 0) | (n ? nav_available_flag : 0) |
                           (r ? root_available_flag : 0) | (texture ? texture_available_flag : 0) |
                           (eligible ? eligible_flag : 0);
        if (last_status != status)
        {
            last_status = status;
            Output::send<LogLevel::Warning>(
                STR("[KH3APProbeCpp] Native area map availability status={} nav={}\n"), status,
                n ? n->GetPathName() : L"none");
        }
#endif
        if (!eligible)
        {
            hide(h);
            pending_toggle = false;
            return;
        }
        // A retained movie belongs only to this playable pawn and area. Resolve
        // its live root again because level changes can close it in the engine.
        if (!opened && !movie_path.empty() &&
            (texture_path != texture->GetPathName() || !object(movie(), L"Root") || !chest_root()))
        {
            hide(h);
        }
        if (pending_toggle)
        {
            pending_toggle = false;
            if (opened)
            {
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
                timing.operation = L"hide-cached";
#endif
                suspend();
                return;
            }
            auto* player = movie();
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
            timing.operation = player ? L"open-cached" : L"open-cold";
            const auto movies_start = std::chrono::steady_clock::now();
#endif
            if (!player)
            {
                player = open_movie(asset, config.gfx_paths, map_movie_priority);
                if (player && !area_player.remember(player))
                {
                    movie_bool(player, L"/Script/ScaleformUI.GFxMoviePlayer:Close", L"Unload",
                               true);
                    player = nullptr;
                }
                if (player)
                {
                    movie_path = player->GetPathName();
                    open_chest_movie(asset);
                }
            }
            else
            {
                movie_bool(player, L"/Script/ScaleformUI.GFxMoviePlayer:SetPause",
                           L"bPausePlayback", false);
                movie_bool(chest_player.get(), L"/Script/ScaleformUI.GFxMoviePlayer:SetPause",
                           L"bPausePlayback", false);
            }
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
            timing.movies_ms = std::chrono::duration<double, std::milli>(
                                   std::chrono::steady_clock::now() - movies_start)
                                   .count();
#endif
            if (!player)
            {
                Output::send<LogLevel::Warning>(
                    STR("[KH3APProbeCpp] Native area map OpenMovie failed\n"));
                return;
            }
            opened = true;
            layout_confirmation_pending = true;
            layout_confirmation_attempts = 0;
            clear_chest_icons();
            rendered_chests.clear();
            chest_layout_dirty = true;
            last_layout = now;
            next_layout_probe = {};
            rendered_player.reset();
            // Completion and height may have changed while hidden. Refresh on
            // reopen before revealing either layer, including cached movies.
            next_chests = {};
            chest_schedule = {};
            next_save_points = {};
            Output::send<LogLevel::Warning>(
                STR("[KH3APProbeCpp] Native area map opened player={}\n"), movie_path);
        }
        if (!opened)
        {
            return;
        }
        auto* canvas = object(n, L"m_MapCanvas");
        auto* pc = object(n, L"m_MarkerPc");
        if (!canvas || !pc)
        {
            hide(h);
            return;
        }
        auto number = [&](UObject* o, const wchar_t* name)
        {
            return gfx_number(config.gfx_paths, o, name).value_or(NAN);
        };
        const auto pc_info = gfx_display_info(pc), canvas_info = gfx_display_info(canvas);
        const auto point =
            pc_info && canvas_info
                ? map_overlay_point(pc_info->position[0], pc_info->position[1], pc_info->rotation,
                                    canvas_info->position[0], canvas_info->position[1],
                                    canvas_info->scale[0] / display_percentage_scale,
                                    canvas_info->scale[1] / display_percentage_scale,
                                    canvas_info->rotation)
                : map_overlay_point(number(pc, L"x"), number(pc, L"y"), number(pc, L"rotation"),
                                    number(canvas, L"x"), number(canvas, L"y"),
                                    number(canvas, L"scaleX"), number(canvas, L"scaleY"),
                                    number(canvas, L"rotation"));
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
        if (point && now >= next_minimap_audit)
        {
            next_minimap_audit = now + diagnostic_audit_interval;
            const auto original = map_overlay_point(
                number(pc, L"x"), number(pc, L"y"), number(pc, L"rotation"), number(canvas, L"x"),
                number(canvas, L"y"), number(canvas, L"scaleX"), number(canvas, L"scaleY"),
                number(canvas, L"rotation"));
            const double delta =
                original ? std::hypot(point->x - original->x, point->y - original->y) : NAN;
            const double heading_delta =
                original ? std::abs(point->heading - original->heading) : NAN;
            Output::send<LogLevel::Normal>(L"[KH3APProbeCpp] Area map bulk geometry audit "
                                           L"delta_pixels={} delta_heading={} valid={}\n",
                                           delta, heading_delta,
                                           std::isfinite(delta) &&
                                               delta <= bulk_position_tolerance_pixels &&
                                               std::isfinite(heading_delta) &&
                                               heading_delta <= bulk_heading_tolerance_degrees);
        }
#endif
        if (!point)
        {
            hide(h);
            return;
        }
        auto* player = movie();
        auto* overlay = object(player, L"Root");
        if (!player || now - last_layout > layout_readiness_timeout)
        {
            hide(h);
            return;
        }
        if (!overlay)
        {
            return;
        }
        const auto path = texture->GetPathName();
        if (texture_path != path)
        {
            show_battle_level(false);
            gfx_set(config.gfx_paths, overlay, L"visible", 0, true);
            gfx_set(config.gfx_paths, chest_root(), L"visible", 0, true);
            presented = false;
            clear_chest_icons();
            gfx_numbers(config.gfx_paths,
                        gfx_object(config.gfx_paths,
                                   gfx_object(config.gfx_paths, overlay, L"mapCanvasFade"),
                                   L"graphics"),
                        L"clear", {});
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
            const auto image_start = std::chrono::steady_clock::now();
#endif
            (void)gfx_invoke(config.gfx_paths, overlay, L"loadImage", path.c_str());
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
            const auto metadata_start = std::chrono::steady_clock::now();
            timing.image_ms =
                std::chrono::duration<double, std::milli>(metadata_start - image_start).count();
#endif
            texture_path = path;
            area = area_metadata(texture);
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
            timing.metadata_ms = std::chrono::duration<double, std::milli>(
                                     std::chrono::steady_clock::now() - metadata_start)
                                     .count();
#endif
            rendered_chests.clear();
            chest_schedule = {};
            chest_layout_dirty = true;
            next_chests = {};
            last_chest_count = -1;
            next_save_points = {};
            last_save_point_count = -1;
            layout_width = 0;
            next_layout_probe = {};
            rendered_player.reset();
        }
        auto get = [&](UObject* o, const wchar_t* name)
        {
            return gfx_object(config.gfx_paths, o, name);
        };
        auto set = [&](UObject* o, const wchar_t* name, double value)
        {
            return gfx_set(config.gfx_paths, o, name, value);
        };
        auto boolean = [&](UObject* o, const wchar_t* name, bool value)
        {
            return gfx_set(config.gfx_paths, o, name, value, true);
        };
        double iw = image_width, ih = image_height, sw = layout_width, sh = layout_height;
        bool legend = layout_legend;
        if (now >= next_layout_probe)
        {
            auto* stage = get(overlay, L"stage");
            auto* image_canvas = get(overlay, L"mapCanvas");
            auto* bitmap = gfx_numbers(config.gfx_paths, image_canvas, L"getChildAt", {0});
            iw = number(bitmap, L"width");
            ih = number(bitmap, L"height");
            sw = number(stage, L"stageWidth");
            sh = number(stage, L"stageHeight");
            if (!std::isfinite(iw) || !std::isfinite(ih) || iw <= 0 || ih <= 0 ||
                !std::isfinite(sw) || !std::isfinite(sh) || sw <= 0 || sh <= 0)
            {
                return;
            }
            const auto state = reconciliation::view();
            const auto palette = state->chests;
            const bool show_legend = icon_enabled(MapIconOption::Chests) &&
                                     icon_enabled(MapIconOption::LargeMap) &&
                                     map_chest_palette_matches(palette.get(), GetCurrentProcessId(),
                                                               state->seed, state->slot);
            legend_rows =
                map_chest_legend_rows(show_legend ? palette.get() : nullptr, map_chest_keyblades);
            sync_keyblade_ownership(*state);
            owned_legend_rows = map_chest_owned_legend_rows(show_legend ? palette.get() : nullptr,
                                                            map_chest_keyblades,
                                                            [&](const std::wstring& token)
                                                            {
                                                                return owns_keyblade(p, token);
                                                            });
            legend = !legend_rows.empty();
            // Scalar dimensions survive frames safely. Retry loading every tick;
            // once valid, check for viewport resizing four times per second.
            next_layout_probe = now + layout_probe_interval;
        }
        const auto legend_height = map_chest_legend_height(legend_rows.size());
        const auto layout = map_overlay_layout(sw, sh, iw, ih, map_keyblade_legend_width,
                                               legend_height, zoom.factor(), point);
        const double fit = layout.fit;
        bool relayout = sw != layout_width || sh != layout_height || iw != image_width ||
                        ih != image_height || legend != layout_legend ||
                        legend_rows != layout_legend_rows || zoom.factor() != layout_zoom;
        bool present_after_repair = false;
        // Scaleform can finish a newly loaded movie's timeline on the update
        // after our writes. Verify that both independent layers retained their
        // transforms/clips before trusting the cached layout or revealing it.
        if (!relayout && layout_confirmation_pending)
        {
            const bool confirmed = confirm_layout(overlay, layout);
            if (!confirmed && ++layout_confirmation_attempts < 3)
            {
                boolean(overlay, L"visible", false);
                boolean(chest_root(), L"visible", false);
                show_battle_level(false);
                presented = false;
                layout_width = 0;
                next_layout_probe = {};
                return;
            }
            layout_confirmation_pending = false;
            layout_confirmation_attempts = 0;
            // Readback is a settling aid, not an availability requirement. If a
            // movie keeps reporting different values, make one final repair and
            // present it instead of trapping the player in an invisible overlay.
            if (!confirmed)
            {
                relayout = present_after_repair = true;
            }
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
            Output::send<LogLevel::Warning>(STR("[KH3APProbeCpp] Area map layout confirmed={} "
                                                "final_repair={} zoom={} map={},{} fit={}\n"),
                                            confirmed, present_after_repair, zoom.factor(), map_x,
                                            map_y, fit);
#endif
        }
        if (relayout)
        {
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
            const auto layout_start = std::chrono::steady_clock::now();
#endif
            auto* map = get(overlay, L"map");
            auto* image_canvas = get(overlay, L"mapCanvas");
            auto* marker = get(overlay, L"markerPc");
            auto* legend_icon = get(chest_root(), L"keybladeLegend");
            auto* viewport = get(chest_root(), L"markerViewport");
            if (!map || !image_canvas || !marker || !legend_icon || !viewport)
            {
                next_layout_probe = {};
                return;
            }
            if (!clip_viewport(overlay, 0, 0, layout.view_width, layout.view_height) ||
                !clip_viewport(viewport, 0, 0, layout.view_width, layout.view_height))
            {
                return;
            }
            // The stock movie contains an authoring-frame shape outside the map.
            // Hide its timeline children, then reveal the actual map container.
            const double children = number(overlay, L"numChildren");
            if (std::isfinite(children) && children >= 0 && children <= overlay_child_limit)
            {
                for (int i = 0; i < static_cast<int>(children); ++i)
                {
                    boolean(gfx_numbers(config.gfx_paths, overlay, L"getChildAt", {double(i)}),
                            L"visible", false);
                }
            }
            boolean(overlay, L"focusable", false);
            boolean(overlay, L"mouseEnabled", false);
            boolean(overlay, L"mouseChildren", false);
            set(overlay, L"alpha", 1);
            boolean(map, L"visible", true);
            set(map, L"alpha", 1);
            // Both movies use an identically positioned, zero-origin viewport.
            // A nonzero scrollRect origin takes effect only when Scaleform
            // renders it, so compensating for it while hidden displaced the
            // native map on its first presentation even after correct readback.
            set(overlay, L"x", layout.view_x);
            set(overlay, L"y", layout.view_y);
            set(viewport, L"x", layout.view_x);
            set(viewport, L"y", layout.view_y);
            // Only the large-map movie reveals the legend. The minimap's
            // copy remains at its authored alpha zero for its entire lifetime.
            set(legend_icon, L"alpha", legend ? 1 : 0);
            set(legend_icon, L"x", layout.legend_x);
            set(legend_icon, L"y", layout.legend_y);
            set(legend_icon, L"scaleX", layout.legend_scale);
            set(legend_icon, L"scaleY", layout.legend_scale);
            if (legend)
            {
                auto* panel = get(legend_icon, L"panel");
                if (!panel)
                {
                    return;
                }
                set(panel, L"scaleY", legend_height / map_keyblade_legend_height);
                for (size_t i = 0; i < map_chest_keyblades.size(); ++i)
                {
                    auto* row = get(legend_icon, (L"row" + std::to_wstring(i)).c_str());
                    if (!row)
                    {
                        return;
                    }
                    const auto found = std::find(legend_rows.begin(), legend_rows.end(), i);
                    const bool included = found != legend_rows.end();
                    boolean(row, L"visible", included);
                    if (included)
                    {
                        set(row, L"y",
                            legend_first_row_y +
                                legend_row_spacing *
                                    static_cast<double>(found - legend_rows.begin()));
                    }
                }
            }
            boolean(legend_icon, L"cacheAsBitmap", true);
            boolean(legend_icon, L"mouseEnabled", false);
            boolean(legend_icon, L"mouseChildren", false);
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
            Output::send<LogLevel::Warning>(
                STR("[KH3APProbeCpp] Map legend enabled={} x={} y={} width={} height={} alpha={} "
                    "children={}\n"),
                legend, number(legend_icon, L"x"), number(legend_icon, L"y"),
                number(legend_icon, L"width"), number(legend_icon, L"height"),
                number(legend_icon, L"alpha"), number(legend_icon, L"numChildren"));
#endif
            set(image_canvas, L"scaleX", fit);
            set(image_canvas, L"scaleY", fit);
            set(image_canvas, L"alpha", map_image_alpha);
            set(marker, L"scaleX", sw / reference_width);
            set(marker, L"scaleY", sw / reference_width);
            boolean(get(overlay, L"markerPcStart"), L"visible", false);
            auto* graphics = get(overlay, L"graphics");
            gfx_numbers(config.gfx_paths, graphics, L"clear", {});
            gfx_numbers(config.gfx_paths, graphics, L"beginFill",
                        {double(panel_background_color), panel_background_alpha});
            gfx_numbers(config.gfx_paths, graphics, L"drawRoundRect",
                        {0, 0, layout.view_width, layout.view_height, panel_corner_radius,
                         panel_corner_radius});
            gfx_numbers(config.gfx_paths, graphics, L"endFill", {});
            layout_width = sw;
            layout_height = sh;
            image_width = iw;
            image_height = ih;
            layout_zoom = zoom.factor();
            layout_legend = legend;
            layout_legend_rows = legend_rows;
            layout_confirmation_pending = !present_after_repair;
            next_chests = {};
            chest_layout_dirty = true;
            next_save_points = {};
            rendered_player.reset();
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
            timing.layout_ms = std::chrono::duration<double, std::milli>(
                                   std::chrono::steady_clock::now() - layout_start)
                                   .count();
#endif
        }
        // Inventory changes only update row opacity, without moving the map or
        // rebuilding the legend. Keep unowned labels/colors recognizable.
        if (legend &&
            (relayout || legend_style_dirty || owned_legend_rows != rendered_owned_legend_rows))
        {
            legend_style_dirty = true;
            auto* legend_icon = get(chest_root(), L"keybladeLegend");
            for (const auto index : legend_rows)
            {
                auto* row = get(legend_icon, (L"row" + std::to_wstring(index)).c_str());
                const bool owned = std::find(owned_legend_rows.begin(), owned_legend_rows.end(),
                                             index) != owned_legend_rows.end();
                if (!row || !set(row, L"alpha", owned ? 1.0 : unowned_legend_alpha))
                {
                    return;
                }
            }
            rendered_owned_legend_rows = owned_legend_rows;
            legend_style_dirty = false;
        }
        if (relayout || map_x != layout.map_x || map_y != layout.map_y)
        {
            auto* map = get(overlay, L"map");
            auto* icons = marker_root();
            if (!set(map, L"x", layout.map_x - layout.view_x) ||
                !set(map, L"y", layout.map_y - layout.view_y) ||
                !set(icons, L"x", layout.map_x - layout.view_x) ||
                !set(icons, L"y", layout.map_y - layout.view_y))
            {
                return;
            }
            map_x = layout.map_x;
            map_y = layout.map_y;
        }
        last_layout = now;
        // A cold open stays hidden for one Scaleform update, then the readback
        // above either confirms the first layout or schedules a full repair.
        if (!presented && layout_confirmation_pending)
        {
            return;
        }
        // The stock keyblade already has its own base orientation. Preserve it;
        // the previous hand-drawn up-arrow introduced a ninety-degree mismatch.
        const std::array<double, 3> pose{point->x * fit, point->y * fit, point->heading};
        if (!rendered_player || *rendered_player != pose)
        {
            auto* marker = get(overlay, L"markerPc");
            if (set(marker, L"x", pose[0]) && set(marker, L"y", pose[1]) &&
                set(marker, L"rotation", pose[2]))
            {
                rendered_player = pose;
            }
        }
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
        const auto chests_start = std::chrono::steady_clock::now();
#endif
        draw_chests(p, overlay, *point, fit, sw, now);
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
        timing.chests_ms = std::chrono::duration<double, std::milli>(
                               std::chrono::steady_clock::now() - chests_start)
                               .count();
        const auto save_points_start = std::chrono::steady_clock::now();
#endif
        draw_save_points(canvas, overlay, fit, sw, now);
        draw_keybinds(p, sw, sh, relayout);
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
        timing.save_points_ms = std::chrono::duration<double, std::milli>(
                                    std::chrono::steady_clock::now() - save_points_start)
                                    .count();
#endif
        if (!presented)
        {
            // Reveal only after the full-size map and its markers have
            // been positioned; early returns above leave both layers hidden.
            boolean(overlay, L"visible", true);
            boolean(chest_root(), L"visible", true);
            presented = true;
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
            Output::send<LogLevel::Warning>(
                STR("[KH3APProbeCpp] Area map presented after layout stage={}x{} image={}x{}\n"),
                sw, sh, iw, ih);
#endif
        }
        show_battle_level(true);
        suppress_native_minimap(p);
        if (!logged)
        {
            logged = true;
            Output::send<LogLevel::Warning>(STR("[KH3APProbeCpp] Native area map active texture={} "
                                                "x={} y={} heading={} stage={}x{} image={}x{}\n"),
                                            path, point->x, point->y, point->heading, sw, sh, iw,
                                            ih);
        }
    }
} // namespace kh3ap
