#include "kh3ap/features/ui/map_overlay/service_impl.h"
#include "kh3ap/features/ui/map_overlay/timing.h"
#include "kh3ap/features/ui/map_overlay/map_chest_movie.h"
#include <DynamicOutput/DynamicOutput.hpp>
#include <algorithm>
#include <cmath>
#include <tuple>
#include <utility>

namespace kh3ap
{
    using namespace RC;
    using namespace RC::Unreal;
    using namespace map_overlay_detail;
    using namespace map_overlay_settings;

    auto RuntimeMapOverlayService::Impl::hide_minimap() -> void
    {
        minimap_layout_cache.clear();
        minimap_projection_cache.clear();
        minimap_frame_inputs.clear();
        minimap_hud_ancestry.clear();
        minimap_canvas_ancestry.clear();
        if (minimap_presented)
        {
            gfx_set(config.gfx_paths, chest_root(true), L"visible", 0, true);
            movie_bool(minimap_player.get(), L"/Script/ScaleformUI.GFxMoviePlayer:SetPause",
                       L"bPausePlayback", true);
        }
        minimap_presented = false;
    }
    auto RuntimeMapOverlayService::Impl::tick_minimap(UObject* p,
                                                      std::chrono::steady_clock::time_point now)
        -> void
    {
        const RuntimeHotPathProfileScope profile{L"map_overlay:minimap_tick"};
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
        MinimapDisplayTimer display_timer;
        std::optional<RuntimeHotPathProfileScope> part;
        auto phase = [&](const wchar_t* name)
        {
            part.reset();
            part.emplace(name);
        };
        phase(L"map_overlay:mini_hud_reads");
#endif
        if (!icon_enabled(MapIconOption::Minimap) || !(icon_options() & map_marker_options))
        {
            hide_minimap();
            return;
        }
        auto [hud, nav] = live_parts(p);
        auto* native = root(nav);
        auto* texture = object(nav, L"m_LoadedAsyncTexture");
        auto* canvas = object(nav, L"m_MapCanvas");
        auto* pc = object(nav, L"m_MarkerPc");
        auto* asset = object(nav, L"m_SwfMovie");
        auto get = [&](UObject* o, const wchar_t* name)
        {
            return gfx_object(config.gfx_paths, o, name);
        };
        auto number = [&](UObject* o, const wchar_t* name)
        {
            return gfx_number(config.gfx_paths, o, name).value_or(NAN);
        };
        auto& parent_inputs = minimap_parent_scratch;
        parent_inputs.clear();
        parent_inputs.reserve(parent_input_reserve);
        const auto append_transform = [&](const GfxDisplayInfo& info)
        {
            for (auto v :
                 {info.position[0], info.position[1], info.scale[0], info.scale[1], info.rotation})
            {
                parent_inputs.push_back(v);
            }
        };
        auto* hud_root = root(hud);
        // Native AS proxies track HUD/movie rebuilds without retaining old
        // member wrappers. All three owning links are read live each frame.
        auto* host = object(object(object(hud, L"m_NaviMap"), L"m_NaviMapCanvas"), L"m_ASObject");
        if (!native || !texture || !canvas || !pc || !asset || !host)
        {
            hide_minimap();
            return;
        }
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
        static bool display_info_checked{};
        if (!display_info_checked)
        {
            display_info_checked = true;
            for (auto* candidate : {pc, canvas, host})
            {
                if (auto info = gfx_display_info(candidate))
                {
                    Output::send<LogLevel::Normal>(
                        L"[KH3APProbeCpp] Minimap display info comparison x={}/{} y={}/{} "
                        L"scale={},{}/{},{} rotation={}/{} alpha={}/{} visible={}/{}\n",
                        info->position[0], number(candidate, L"x"), info->position[1],
                        number(candidate, L"y"), info->scale[0], info->scale[1],
                        number(candidate, L"scaleX"), number(candidate, L"scaleY"), info->rotation,
                        number(candidate, L"rotation"), info->alpha, number(candidate, L"alpha"),
                        info->visible,
                        gfx_number(config.gfx_paths, candidate, L"visible", true).value_or(0));
                }
            }
        }
#endif
        // Read the live display state in one native call per object. Scaleform
        // returns scale/opacity percentages; AS members use unit ratios.
        const auto parent = [&](UObject* o)
        {
            return get(o, L"parent");
        };
        if (minimap_hud_ancestry.sync(host, hud_root, now, parent))
        {
            minimap_projection_cache.clear();
            minimap_frame_inputs.clear();
        }
        if (!minimap_hud_ancestry.visit(
                [&](UObject* ancestor)
                {
                    const auto info = gfx_display_info(ancestor);
                    const bool visible =
                        info ? info->visible
                             : gfx_number(config.gfx_paths, ancestor, L"visible", true)
                                       .value_or(0) != 0;
                    const double alpha =
                        info ? info->alpha / display_percentage_scale : number(ancestor, L"alpha");
                    if (!visible || !std::isfinite(alpha) || alpha <= minimum_visible_alpha)
                    {
                        return false;
                    }
                    if (info)
                    {
                        append_transform(*info);
                    }
                    else
                    {
                        parent_inputs.push_back(
                            NAN); // Unsupported bulk reads use the original path.
                    }
                    return true;
                }))
        {
            hide_minimap();
            return;
        }
        if (minimap_texture.Get() != texture)
        {
            const auto path = texture->GetPathName();
            minimap_texture = make_uobject_weak(texture);
            if (path != minimap_texture_path)
            {
                hide_minimap();
                minimap_texture_path = path;
                minimap_area = area_metadata(texture);
                chest_schedule = {};
                next_minimap_markers = {};
                minimap_presented = false;
                // Keep cold metadata, UI setup and discovery on separate ticks.
                return;
            }
        }
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
        phase(L"map_overlay:mini_player_geometry");
#endif
        auto* icons = marker_root(true);
        const auto viewport = viewport_size(p);
        if (minimap_layout_cache.sync({hud_root, native, canvas, texture, icons}, viewport))
        {
            minimap_projection_cache.clear();
            minimap_frame_inputs.clear();
        }
        if (minimap_input_host.Get() != host)
        {
            minimap_input_host = make_uobject_weak(host);
            minimap_projection_cache.clear();
            minimap_frame_inputs.clear();
        }
        auto* bitmap = minimap_layout_cache.object(MinimapLayoutCache::Bitmap,
                                                   [&]
                                                   {
                                                       return gfx_numbers(config.gfx_paths, canvas,
                                                                          L"getChildAt", {0});
                                                   });
        const auto image = minimap_layout_cache.image_size(
            [&]
            {
                return std::array{number(bitmap, L"width"), number(bitmap, L"height")};
            });
        if (!image)
        {
            hide_minimap();
            return;
        }
        const auto [iw, ih] = *image;
        const auto sora = actor_location(p);
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
        bool audit_layout = false;
        if (point && now >= next_minimap_audit)
        {
            audit_layout = true;
            next_minimap_audit = now + diagnostic_audit_interval;
            const auto original = map_overlay_point(
                number(pc, L"x"), number(pc, L"y"), number(pc, L"rotation"), number(canvas, L"x"),
                number(canvas, L"y"), number(canvas, L"scaleX"), number(canvas, L"scaleY"),
                number(canvas, L"rotation"));
            const double delta =
                original ? std::hypot(point->x - original->x, point->y - original->y) : NAN;
            Output::send<LogLevel::Normal>(
                L"[KH3APProbeCpp] Minimap bulk projection audit delta_pixels={} valid={}\n", delta,
                std::isfinite(delta) && delta <= bulk_position_tolerance_pixels);
        }
#endif
        if (!minimap_area || !sora || !point || !std::isfinite(iw) || !std::isfinite(ih) ||
            iw <= 0 || ih <= 0)
        {
            hide_minimap();
            return;
        }
        const auto expected = map_overlay_world_point(*minimap_area, (*sora)[0], (*sora)[1]);
        if (!expected || std::hypot(expected->x - point->x, expected->y - point->y) >
                             world_alignment_tolerance_pixels)
        {
            hide_minimap();
            return;
        }
        // Retain live ancestry reads: an AS reparent or animation can change
        // the concatenated matrix without replacing the owning HUD/movie.
        if (minimap_canvas_ancestry.sync(canvas, native, now, parent))
        {
            minimap_projection_cache.clear();
            minimap_frame_inputs.clear();
        }
        bool canvas_head = true;
        if (!minimap_canvas_ancestry.visit(
                [&](UObject* ancestor)
                {
                    if (std::exchange(canvas_head, false))
                    {
                        return true; // Local canvas is applied below.
                    }
                    const auto info = gfx_display_info(ancestor);
                    if (info)
                    {
                        append_transform(*info);
                    }
                    else
                    {
                        parent_inputs.push_back(NAN);
                    }
                    return true;
                }))
        {
            hide_minimap();
            return;
        }
        auto& frame_inputs = minimap_frame_scratch;
        frame_inputs.assign(parent_inputs.begin(), parent_inputs.end());
        frame_inputs.reserve(frame_input_reserve);
        for (double v : {iw, ih, point->x, point->y, point->heading})
        {
            frame_inputs.push_back(v);
        }
        for (auto v : *sora)
        {
            frame_inputs.push_back(v);
        }
        for (const auto& info : {pc_info, canvas_info})
        {
            if (info)
            {
                for (auto v : {info->position[0], info->position[1], info->scale[0], info->scale[1],
                               info->rotation})
                {
                    frame_inputs.push_back(v);
                }
            }
            else
            {
                frame_inputs.push_back(NAN);
            }
        }
        // Preserve live visibility/player checks, but skip unchanged layout,
        // projection and display writes between semantic marker refreshes.
        if (minimap_presented && now < next_minimap_markers && frame_inputs == minimap_frame_inputs
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
            && !audit_layout
#endif
        )
            return;
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
        phase(L"map_overlay:mini_projection_reads");
#endif
        auto* native_stage = minimap_layout_cache.object(MinimapLayoutCache::NativeStage,
                                                         [&]
                                                         {
                                                             return get(native, L"stage");
                                                         });
        auto* hud_stage = minimap_layout_cache.object(MinimapLayoutCache::HudStage,
                                                      [&]
                                                      {
                                                          return get(hud_root, L"stage");
                                                      });
        if (!icons)
        {
            open_chest_movie(asset, true);
            return; // OpenMovie leaves the new layer hidden until it is drawn.
        }
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
        display_timer.first = !minimap_presented;
        display_timer.stage(display_timer.context_ms);
#endif
        if (!minimap_presented)
        {
            movie_bool(minimap_player.get(), L"/Script/ScaleformUI.GFxMoviePlayer:SetPause",
                       L"bPausePlayback", false);
        }
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
        display_timer.stage(display_timer.activation_ms);
#endif
        auto* stage = minimap_layout_cache.object(MinimapLayoutCache::IconStage,
                                                  [&]
                                                  {
                                                      return get(icons, L"stage");
                                                  });
        const auto stages = minimap_layout_cache.stage_sizes(
            [&]
            {
                return std::array{
                    number(stage, L"stageWidth"),        number(stage, L"stageHeight"),
                    number(hud_stage, L"stageWidth"),    number(hud_stage, L"stageHeight"),
                    number(native_stage, L"stageWidth"), number(native_stage, L"stageHeight")};
            });
        if (!stages)
        {
            hide_minimap();
            return;
        }
        const auto [sw, sh, hud_w, hud_h, source_w, source_h] = *stages;
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
        if (audit_layout)
        {
            const auto bounds =
                gfx_object_argument(config.gfx_paths, host, L"getBounds", hud_stage);
            const auto old_host =
                get(get(get(hud_root, L"hudRight"), L"naviMap"), L"naviMapCanvas");
            const auto old_bounds =
                gfx_object_argument(config.gfx_paths, old_host, L"getBounds", hud_stage);
            double delta = 0;
            const auto compare = [&](double a, double b)
            {
                delta = std::isfinite(a) && std::isfinite(b) ? std::max(delta, std::abs(a - b))
                                                             : INFINITY;
            };
            compare(iw, number(bitmap, L"width"));
            compare(ih, number(bitmap, L"height"));
            for (const auto& [owner, width, height] :
                 {std::tuple{get(icons, L"stage"), sw, sh},
                  std::tuple{get(hud_root, L"stage"), hud_w, hud_h},
                  std::tuple{get(native, L"stage"), source_w, source_h}})
            {
                compare(width, number(owner, L"stageWidth"));
                compare(height, number(owner, L"stageHeight"));
            }
            for (const auto* field : {L"x", L"y", L"width", L"height"})
            {
                compare(number(bounds, field), number(old_bounds, field));
            }
            Output::send<LogLevel::Normal>(
                STR("[KH3APProbeCpp] Minimap layout cache audit viewport={}x{} stage={}x{} "
                    "image={}x{} delta={} valid={}\n"),
                viewport ? (*viewport)[0] : 0, viewport ? (*viewport)[1] : 0, sw, sh, iw, ih, delta,
                viewport.has_value() && delta <= bulk_position_tolerance_pixels);
        }
#endif
        const auto read_projection = [&]() -> std::optional<MinimapIconProjection>
        {
            const auto bounds =
                gfx_object_argument(config.gfx_paths, host, L"getBounds", hud_stage);
            const auto transform = get(get(canvas, L"transform"), L"concatenatedMatrix");
            if (!bounds || !transform)
            {
                return {};
            }
            return MinimapIconProjection{{number(transform, L"a"), number(transform, L"b"),
                                          number(transform, L"c"), number(transform, L"d"),
                                          number(transform, L"tx"), number(transform, L"ty")},
                                         source_w,
                                         source_h,
                                         number(bounds, L"x") * sw / hud_w,
                                         number(bounds, L"y") * sh / hud_h,
                                         number(bounds, L"width") * sw / hud_w,
                                         number(bounds, L"height") * sh / hud_h};
        };
        for (double v : *stages)
        {
            parent_inputs.push_back(v);
        }
        std::optional<MinimapIconProjection> cached_projection;
        if (canvas_info)
        {
            cached_projection = minimap_projection_cache.resolve(
                minimap_local_matrix(canvas_info->position[0], canvas_info->position[1],
                                     canvas_info->scale[0] / display_percentage_scale,
                                     canvas_info->scale[1] / display_percentage_scale,
                                     canvas_info->rotation),
                parent_inputs, read_projection);
        }
        if (!cached_projection)
        {
            cached_projection = read_projection();
        }
        if (!cached_projection)
        {
            hide_minimap();
            return;
        }
        const auto projection = *cached_projection;
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
        if (audit_layout)
        {
            const auto original = read_projection();
            double delta = 0;
            for (const auto& p :
                 {std::pair{0., 0.}, std::pair{iw, ih},
                  std::pair{iw * projection_probe_x_fraction, ih * projection_probe_y_fraction}})
            {
                const auto actual = projection.canvas.point(p.first, p.second);
                const auto expected = original ? original->canvas.point(p.first, p.second)
                                               : MapOverlayPoint{NAN, NAN, 0};
                delta = std::max(delta, std::hypot(actual.x - expected.x, actual.y - expected.y));
            }
            if (original)
            {
                for (const auto& p :
                     {std::pair{projection.x, original->x}, std::pair{projection.y, original->y},
                      std::pair{projection.width, original->width},
                      std::pair{projection.height, original->height}})
                {
                    delta = std::max(delta, std::abs(p.first - p.second));
                }
            }
            const bool valid =
                original && std::isfinite(delta) && delta <= projection_audit_tolerance_pixels;
            Output::send<LogLevel::Normal>(
                L"[KH3APProbeCpp] Minimap parent projection cache audit delta_pixels={} valid={}\n",
                delta, valid);
            if (!valid)
            {
                minimap_projection_cache.clear();
                hide_minimap();
                return;
            }
        }
#endif
        const double size = minimap_icon_size * std::clamp(sw / reference_width, minimum_icon_scale,
                                                           maximum_icon_scale);
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
        display_timer.stage(display_timer.projection_ms);
#endif
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
        phase(L"map_overlay:mini_marker_collection");
#endif
        if (now >= next_minimap_markers || !minimap_presented)
        {
            if (!minimap_presented)
            {
                chest_schedule = {};
            }
            collect_markers(p, sora, minimap_area, iw, ih, 1, now, minimap_markers,
                            minimap_positions);
            next_minimap_markers = now + marker_refresh_interval;
        }
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
        display_timer.stage(display_timer.collection_ms);
#endif
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
        phase(L"map_overlay:mini_marker_draw");
#endif
        auto& visible = minimap_scratch;
        visible.clear();
        visible.reserve(minimap_markers.size());
        for (const auto& marker : minimap_markers)
        {
            const auto position =
                projection.project(marker.x, marker.y, size * minimap_clip_radius_fraction);
            if (position)
            {
                visible.push_back(
                    {position->x, position->y, 0, marker.kind, marker.frame, marker.alpha});
            }
        }
        bool rebuild = !minimap_presented || minimap_art_size != size ||
                       visible.size() != rendered_minimap.size();
        if (!rebuild)
        {
            for (size_t i = 0; i < visible.size(); ++i)
            {
                if (visible[i].kind != rendered_minimap[i].kind ||
                    visible[i].frame != rendered_minimap[i].frame)
                {
                    rebuild = true;
                    break;
                }
            }
        }
        if (rebuild)
        {
            clear_chest_icons(true);
        }
        int chest_count = 0, emblem_count = 0, ingredient_count = 0;
        bool complete = true;
        for (size_t i = 0; i < visible.size(); ++i)
        {
            const auto& marker = visible[i];
            const bool emblem = marker.kind == MarkerKind::Emblem,
                       ingredient = marker.kind == MarkerKind::Ingredient;
            auto& count = ingredient ? ingredient_count : emblem ? emblem_count : chest_count;
            const auto index = count++;
            if (!rebuild && marker == rendered_minimap[i])
            {
                continue;
            }
            auto* icon = minimap_icon(icons, marker.kind, index);
            if (!is_uobject_valid_fast(icon))
            {
                complete = false;
                continue;
            }
            if (!gfx_position(icon, marker.x, marker.y))
            {
                gfx_set(config.gfx_paths, icon, L"x", marker.x);
                gfx_set(config.gfx_paths, icon, L"y", marker.y);
            }
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
            if (!minimap_position_audited)
            {
                minimap_position_audited = true;
                const double delta =
                    std::hypot(number(icon, L"x") - marker.x, number(icon, L"y") - marker.y);
                Output::send<LogLevel::Normal>(
                    L"[KH3APProbeCpp] Minimap bulk position audit delta_pixels={} valid={}\n",
                    delta, std::isfinite(delta) && delta <= projection_audit_tolerance_pixels);
            }
#endif
            gfx_set(config.gfx_paths, icon, L"alpha", marker.alpha);
            if (rebuild)
            {
                if (marker.kind == MarkerKind::Chest)
                {
                    gfx_numbers(config.gfx_paths, icon, L"gotoAndStop", {double(marker.frame)});
                }
                const double scale = size / (ingredient ? map_ingredient_art_width
                                             : emblem   ? map_emblem_art_width
                                                        : map_chest_art_width);
                gfx_set(config.gfx_paths, icon, L"cacheAsBitmap", 1, true);
                gfx_set(config.gfx_paths, icon, L"scaleX", scale);
                gfx_set(config.gfx_paths, icon, L"scaleY", scale);
            }
        }
        minimap_chests = chest_count;
        minimap_emblems = emblem_count;
        minimap_ingredients = ingredient_count;
        rendered_minimap.swap(visible);
        minimap_art_size = size;
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
        display_timer.stage(display_timer.draw_ms);
#endif
        if (!minimap_presented)
        {
            gfx_set(config.gfx_paths, chest_root(true), L"visible", 1, true);
        }
        minimap_presented = complete;
        if (complete)
        {
            minimap_frame_inputs.swap(frame_inputs);
        }
        else
        {
            minimap_frame_inputs.clear();
        }
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
        display_timer.stage(display_timer.reveal_ms);
        if (last_minimap_count != static_cast<int>(rendered_minimap.size()))
        {
            last_minimap_count = static_cast<int>(rendered_minimap.size());
            Output::send<LogLevel::Warning>(
                STR("[KH3APProbeCpp] Minimap icons chests={} emblems={} ingredients={} "
                    "source={}x{} bounds={},{},{}x{}\n"),
                chest_count, emblem_count, ingredient_count, projection.source_width,
                projection.source_height, projection.x, projection.y, projection.width,
                projection.height);
        }
#endif
    }
} // namespace kh3ap
