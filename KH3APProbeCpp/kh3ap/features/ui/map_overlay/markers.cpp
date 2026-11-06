#include <array>
#include <chrono>
#include <optional>
#include <utility>
#include "kh3ap/features/ui/map_overlay/service_impl.h"
#include "kh3ap/features/ui/map_overlay/map_chest_movie.h"
#include "kh3ap/core/objects/actor_discovery.h"
#include "kh3ap/core/objects/asset_load_helpers.h"
#include "kh3ap/features/grants/abilities/ability_snapshot_helpers.h"
#include "kh3ap/features/treasure/chest_persistence_service.h"
#include <DynamicOutput/DynamicOutput.hpp>
#include <Unreal/AActor.hpp>
#include <Unreal/World.hpp>
#include <algorithm>
#include <cmath>
#include <exception>
#include <span>
#include <stdexcept>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

namespace kh3ap
{
    namespace
    {
        constexpr auto first_keyblade_marker_frame = 2;

        constexpr auto max_map_projection_error_pixels = 3;
        constexpr auto map_projection_deferred_count = -2;
        constexpr auto native_marker_canvas_child_count = 6;
        constexpr auto native_marker_layer_index = 3;
    } // namespace

    using namespace RC;
    using namespace RC::Unreal;
    using namespace map_overlay_detail;
    using namespace map_overlay_settings;

    auto RuntimeMapOverlayService::Impl::enabled() const -> bool
    {
        return !config.enabled || config.enabled();
    }
    auto RuntimeMapOverlayService::Impl::icon_options() const -> uint32_t
    {
        return config.icon_options ? config.icon_options() : map_icon_defaults;
    }
    auto RuntimeMapOverlayService::Impl::icon_enabled(MapIconOption option) const -> bool
    {
        return map_icon_enabled(icon_options(), option);
    }
    auto RuntimeMapOverlayService::Impl::sync_keyblade_ownership(const reconciliation::View& state)
        -> void
    {
        if (ownership_epoch != state.epoch || ownership_seed != state.seed ||
            ownership_slot != state.slot)
        {
            keyblade_ownership.clear();
            ownership_epoch = state.epoch;
            ownership_seed = state.seed;
            ownership_slot = state.slot;
        }
    }
    auto RuntimeMapOverlayService::Impl::owns_keyblade(UObject* p, const std::wstring& token)
        -> bool
    {
        try
        {
            return keyblade_ownership
                .get(token,
                     [&]
                     {
                         const auto owned =
                             config.owns_keyblade ? config.owns_keyblade(p, token) : std::nullopt;
                         if (!owned)
                         {
                             throw std::runtime_error("map_keyblade_inventory_unavailable");
                         }
                         return *owned;
                     })
                .owned;
        }
        catch (const std::exception&)
        {
            return false;
        }
    }
    auto RuntimeMapOverlayService::Impl::collect_markers(
        UObject* p, const std::optional<std::array<float, 3>>& sora,
        const std::optional<MapOverlayArea>& area, double image_width, double image_height,
        double fit, std::chrono::steady_clock::time_point now, std::vector<ChestMarker>& markers,
        LandmarkPositions& positions) -> void
    {
        auto* world = static_cast<AActor*>(p)->GetWorld();
        if (chest_world.Get() != world)
        {
            chests.clear();
            chest_discovery.request_scan();
            large_positions = {};
            minimap_positions = {};
            chest_world = make_uobject_weak(world);
        }
        const auto work = chest_schedule.take(now);
        bool roster_changed = std::erase_if(chests,
                                            [](const Chest& entry)
                                            {
                                                return !entry.actor.Get();
                                            }) != 0;
        const auto remember = [&](UObject* chest)
        {
            if (!is_uobject_valid_fast(chest) ||
                chest->HasAnyFlags(static_cast<EObjectFlags>(RF_NeedLoad | RF_NeedPostLoad |
                                                             RF_NeedPostLoadSubobjects)))
            {
                return;
            }
            // Streamed actors' package UWorld differs from the live OwningWorld.
            auto* owner = object(chest->GetOuterPrivate(), L"OwningWorld");
            if (!owner)
            {
                owner = static_cast<AActor*>(chest)->GetWorld();
            }
            if (owner != world || is_fake_chest(chest))
            {
                return;
            }
            auto found = std::find_if(chests.begin(), chests.end(),
                                      [&](const Chest& entry)
                                      {
                                          return entry.actor.Get() == chest;
                                      });
            if (found != chests.end())
            {
                found->located = false;
                roster_changed = true;
                return;
            }
            Chest entry;
            entry.actor = make_uobject_weak(chest);
            if (entry.actor.Get() != chest)
            {
                return;
            }
            if (auto* id = chest->GetPropertyByNameInChain(L"m_TreasureID");
                id && id->GetClass().GetName() == L"NameProperty" &&
                id->GetElementSize() == sizeof(FName))
            {
                entry.treasure = id->ContainerPtrToValuePtr<FName>(chest)->ToString();
            }
            chests.push_back(std::move(entry));
            roster_changed = true;
        };
        if (chest_discovery.pending())
        {
            auto batch = chest_discovery.take();
            if (batch.scan)
            {
                const RuntimeHotPathProfileScope profile{L"map_overlay:chest_discovery"};
                auto* klass = find_loaded_runtime_asset_class(L"/Script/TresGame.TresTreasureBox");
                const auto actors = find_live_actors_of_class(p, klass, chest_discovery_limit);
                chests.clear();
                if (actors.size() <= chest_discovery_limit)
                {
                    for (auto* chest : actors)
                    {
                        remember(chest);
                    }
                }
                roster_changed = true;
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
                ++chest_discovery_scans;
#endif
            }
            else
            {
                const RuntimeHotPathProfileScope profile{L"map_overlay:chest_arrivals"};
                for (auto& weak : batch.actors)
                {
                    if (auto* chest = weak.Get())
                    {
                        remember(chest);
                    }
                }
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
                ++chest_event_batches;
#endif
            }
        }
        const auto update_completion = [&]
        {
            const RuntimeHotPathProfileScope profile{L"map_overlay:chest_completion"};
            for (auto& entry : chests)
            {
                auto* chest = entry.actor.Get();
                entry.unopened = false;
                if (!is_uobject_valid_fast(chest))
                {
                    continue;
                }
                if (!entry.located)
                {
                    if (const auto location = actor_location(chest))
                    {
                        entry.location = *location;
                        entry.located = true;
                    }
                }
                try
                {
                    entry.unopened =
                        !object(chest, L"m_pOpenner") && !reconcile_chest_record(chest, false);
                }
                catch (const std::exception&)
                {
                } // Unknown completion state stays hidden.
            }
        };
        if (work.completion || roster_changed)
        {
            update_completion();
        }
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
        if (work.completion || roster_changed)
        {
            std::wstring unopened;
            for (const auto& entry : chests)
            {
                if (entry.unopened)
                {
                    if (!unopened.empty())
                    {
                        unopened += L",";
                    }
                    unopened += entry.treasure;
                }
            }
            Output::send<LogLevel::Normal>(STR("[KH3APProbeCpp] Map chest roster scans={} "
                                               "event_batches={} loaded={} unopened={}\n"),
                                           chest_discovery_scans, chest_event_batches,
                                           chests.size(), unopened);
        }
#endif
        markers.clear();
        markers.reserve(std::min<size_t>(chests.size(), chest_icon_limit));
        const auto state = reconciliation::view();
        const auto palette = state->chests;
        const bool active_palette = map_chest_palette_matches(palette.get(), GetCurrentProcessId(),
                                                              state->seed, state->slot);
        sync_keyblade_ownership(*state);
        const auto owns = [&](const std::wstring& token)
        {
            return owns_keyblade(p, token);
        };
        for (const auto& entry : chests)
        {
            if (!icon_enabled(MapIconOption::Chests))
            {
                break;
            }
            if (!entry.unopened || !entry.located || !entry.actor.Get())
            {
                continue;
            }
            const double alpha =
                map_chest_alpha(active_palette ? palette.get() : nullptr, entry.treasure,
                                icon_enabled(MapIconOption::LockedChests), owns);
            if (alpha <= 0)
            {
                continue;
            }
            const auto pos = map_overlay_world_point(*area, entry.location[0], entry.location[1]);
            if (!pos || !map_overlay_contains(*pos, image_width, image_height))
            {
                continue;
            }
            const double height = entry.location[2] - (*sora)[2];
            int frame = 1;
            if (active_palette)
            {
                const auto assigned = palette->map_keyblades.find(entry.treasure);
                if (assigned != palette->map_keyblades.end())
                {
                    const auto color = std::find(map_chest_keyblades.begin(),
                                                 map_chest_keyblades.end(), assigned->second.name);
                    if (color != map_chest_keyblades.end())
                    {
                        frame = first_keyblade_marker_frame +
                                static_cast<int>(color - map_chest_keyblades.begin());
                    }
                }
            }
            markers.push_back(
                {pos->x * fit, pos->y * fit,
                 std::abs(height) > marker_height_threshold ? (height > 0 ? -1 : 1) : 0,
                 MarkerKind::Chest, frame, alpha});
            if (markers.size() == chest_icon_limit)
            {
                break;
            }
        }
        // The existing tracker owns weak component references. Snapshot only
        // while visible, on the same half-second gate as chest height checks.
        // Fresh native completion also handles captures made while hidden.
        if (config.lucky_emblems && icon_enabled(MapIconOption::Emblems))
        {
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
            RuntimeHotPathProfileScope emblem_profile{L"map_overlay:emblem_snapshot_projection"};
#endif
            config.lucky_emblems(p, work.discover_other, emblem_snapshot);
            positions.emblems.sync(std::span<const RuntimeLuckyEmblemMarker>{emblem_snapshot},
                                   *area, image_width, image_height, map_overlay_world_point);
            int count = 0;
            for (size_t i = 0; i < emblem_snapshot.size(); ++i)
            {
                const auto& pos = positions.emblems.point(i);
                if (!pos)
                {
                    continue;
                }
                const double height = emblem_snapshot[i].location[2] - (*sora)[2];
                markers.push_back(
                    {pos->x * fit, pos->y * fit,
                     std::abs(height) > marker_height_threshold ? (height > 0 ? -1 : 1) : 0,
                     MarkerKind::Emblem});
                if (++count == emblem_icon_limit)
                {
                    break;
                }
            }
        }
        if (config.ingredients && icon_enabled(MapIconOption::Ingredients))
        {
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
            RuntimeHotPathProfileScope ingredient_profile{
                L"map_overlay:ingredient_snapshot_projection"};
#endif
            config.ingredients(work.discover_other, ingredient_snapshot);
            positions.ingredients.sync(
                std::span<const RuntimeIngredientMarker>{ingredient_snapshot}, *area, image_width,
                image_height, map_overlay_world_point);
            int count = 0;
            for (size_t i = 0; i < ingredient_snapshot.size(); ++i)
            {
                const auto& pos = positions.ingredients.point(i);
                if (!pos)
                {
                    continue;
                }
                const double height = ingredient_snapshot[i].location[2] - (*sora)[2];
                markers.push_back(
                    {pos->x * fit, pos->y * fit,
                     std::abs(height) > marker_height_threshold ? (height > 0 ? -1 : 1) : 0,
                     MarkerKind::Ingredient});
                if (++count == ingredient_icon_limit)
                {
                    break;
                }
            }
        }
    }
    auto RuntimeMapOverlayService::Impl::draw_chests(UObject* p, UObject* overlay,
                                                     const MapOverlayPoint& pc, double fit,
                                                     double sw,
                                                     std::chrono::steady_clock::time_point now)
        -> void
    {
        if (now < next_chests)
        {
            return;
        }
        next_chests = now + marker_refresh_interval;
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
        RuntimeHotPathProfileScope profile{L"map_overlay:marker_refresh"};
#endif
        auto* layer = gfx_object(config.gfx_paths, overlay, L"mapCanvasFade");
        auto* graphics = gfx_object(config.gfx_paths, layer, L"graphics");
        if (!graphics)
        {
            return;
        }
        auto* icons = marker_root();
        if (!icon_enabled(MapIconOption::LargeMap))
        {
            clear_chest_icons();
            rendered_chests.clear();
            chest_layout_dirty = true;
            gfx_numbers(config.gfx_paths, graphics, L"clear", {});
            return;
        }
        const auto sora = actor_location(p);
        if (!area || !sora)
        {
            clear_chest_icons();
            rendered_chests.clear();
            chest_layout_dirty = true;
            gfx_numbers(config.gfx_paths, graphics, L"clear", {});
            return;
        }
        const auto expected = map_overlay_world_point(*area, (*sora)[0], (*sora)[1]);
        const double error =
            expected ? std::hypot(expected->x - pc.x, expected->y - pc.y) : INFINITY;
        // Native marker and actor snapshots can be a frame apart during movement.
        // A larger disagreement means this map uses a different projection.
        if (error > max_map_projection_error_pixels)
        {
            if (!rendered_chests.empty())
            {
                clear_chest_icons();
                rendered_chests.clear();
                chest_layout_dirty = true;
                gfx_numbers(config.gfx_paths, graphics, L"clear", {});
            }
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
            if (last_chest_count != map_projection_deferred_count)
            {
                Output::send<LogLevel::Warning>(
                    STR("[KH3APProbeCpp] Area map chest projection deferred texture={} error={} "
                        "native={},{} expected={},{}\n"),
                    texture_path, error, pc.x, pc.y, expected ? expected->x : 0,
                    expected ? expected->y : 0);
            }
#endif
            last_chest_count = map_projection_deferred_count;
            return;
        }
        auto& markers = large_marker_scratch;
        collect_markers(p, sora, area, image_width, image_height, fit, now, markers,
                        large_positions);
        if (!chest_layout_dirty && markers == rendered_chests)
        {
            return;
        }
        gfx_numbers(config.gfx_paths, graphics, L"clear", {});
        clear_chest_icons();
        gfx_set(config.gfx_paths, layer, L"visible", 1, true);
        for (auto* field : {L"x", L"y", L"rotation"})
        {
            gfx_set(config.gfx_paths, layer, field, 0);
        }
        for (auto* field : {L"scaleX", L"scaleY", L"alpha"})
        {
            gfx_set(config.gfx_paths, layer, field, 1);
        }
        int count = 0;
        int emblem_count = 0;
        int ingredient_count = 0;
        const double base_size =
            std::clamp(sw / reference_width, minimum_icon_scale, maximum_icon_scale);
        auto call = [&](const wchar_t* method, std::initializer_list<double> values)
        {
            gfx_numbers(config.gfx_paths, graphics, method, values);
        };
        bool complete = true;
        for (const auto& marker : markers)
        {
            const bool emblem = marker.kind == MarkerKind::Emblem;
            const bool ingredient = marker.kind == MarkerKind::Ingredient;
            const double size =
                base_size * (marker.kind == MarkerKind::Chest ? zoom.chest_factor() : 1.);
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
            RuntimeHotPathProfileScope marker_profile{ingredient ? L"map_overlay:ingredient_draw"
                                                      : emblem   ? L"map_overlay:emblem_draw"
                                                                 : L"map_overlay:chest_draw"};
#endif
            const double x = marker.x, y = marker.y;
            auto& icon_count = ingredient ? ingredient_count : emblem ? emblem_count : count;
            auto* icon = gfx_object(config.gfx_paths, icons,
                                    ((ingredient ? L"ingredient"
                                      : emblem   ? L"emblem"
                                                 : L"chest") +
                                     std::to_wstring(icon_count))
                                        .c_str());
            if (!is_uobject_valid_fast(icon))
            {
                complete = false;
                continue;
            }
            if (marker.kind == MarkerKind::Chest)
            {
                gfx_numbers(config.gfx_paths, icon, L"gotoAndStop", {double(marker.frame)});
            }
            gfx_set(config.gfx_paths, icon, L"cacheAsBitmap", 1, true);
            gfx_set(config.gfx_paths, icon, L"x", x);
            gfx_set(config.gfx_paths, icon, L"y", y);
            const double icon_scale = large_icon_size * size /
                                      (ingredient ? map_ingredient_art_width
                                       : emblem   ? map_emblem_art_width
                                                  : map_chest_art_width);
            gfx_set(config.gfx_paths, icon, L"scaleX", icon_scale);
            gfx_set(config.gfx_paths, icon, L"scaleY", icon_scale);
            gfx_set(config.gfx_paths, icon, L"alpha", marker.alpha);
            if (marker.height)
            {
                const double direction = marker.height;
                call(L"lineStyle",
                     {height_arrow_stroke_width * size, double(height_arrow_color), marker.alpha});
                call(L"moveTo", {x - height_arrow_half_width * size,
                                 y + direction * height_arrow_base_offset * size});
                call(L"lineTo", {x, y + direction * height_arrow_tip_offset * size});
                call(L"lineTo", {x + height_arrow_half_width * size,
                                 y + direction * height_arrow_base_offset * size});
            }
            ++icon_count;
        }
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
        if (ingredient_count != last_ingredient_count)
        {
            Output::send<LogLevel::Warning>(
                STR("[KH3APProbeCpp] Area map ingredients texture={} shown={}\n"), texture_path,
                ingredient_count);
        }
        if (emblem_count != last_emblem_count)
        {
            Output::send<LogLevel::Warning>(
                STR("[KH3APProbeCpp] Area map lucky emblems texture={} shown={}\n"), texture_path,
                emblem_count);
        }
        if (count != last_chest_count)
        {
            Output::send<LogLevel::Warning>(STR("[KH3APProbeCpp] Area map chests texture={} "
                                                "shown={} cached={} projection_error={}\n"),
                                            texture_path, count, chests.size(), error);
        }
#endif
        last_chest_count = count;
        last_emblem_count = emblem_count;
        last_ingredient_count = ingredient_count;
        displayed_icons = count;
        displayed_emblems = emblem_count;
        displayed_ingredients = ingredient_count;
        rendered_chests.swap(markers);
        chest_layout_dirty = !complete;
    }
    auto RuntimeMapOverlayService::Impl::draw_save_points(UObject* native_canvas, UObject* overlay,
                                                          double fit, double sw,
                                                          std::chrono::steady_clock::time_point now)
        -> void
    {
        if (now < next_save_points)
        {
            return;
        }
        next_save_points = now + marker_refresh_interval;
        auto number = [&](UObject* o, const wchar_t* name)
        {
            return gfx_number(config.gfx_paths, o, name).value_or(NAN);
        };
        auto visible = [&](UObject* o)
        {
            return gfx_number(config.gfx_paths, o, L"visible", true).value_or(0) != 0;
        };
        auto child = [&](UObject* o, int i)
        {
            return gfx_numbers(config.gfx_paths, o, L"getChildAt", {double(i)});
        };
        auto* canvas = gfx_object(config.gfx_paths, overlay, L"mapCanvas");
        // NaviMapGra inserts its loaded bitmap at zero, followed by marker
        // layers 4,3,2,1,0. Layer 2 is the native save-point layer. Resolve it
        // each refresh instead of retaining a GFx UObject across travel/GC.
        if (number(canvas, L"numChildren") != native_marker_canvas_child_count)
        {
            return;
        }
        auto* target_layer = child(canvas, native_marker_layer_index);
        const auto allocated = number(target_layer, L"numChildren");
        if (!std::isfinite(allocated) || allocated < 0 || allocated > save_point_marker_limit)
        {
            return;
        }
        for (int i = 0; i < static_cast<int>(allocated); ++i)
        {
            gfx_set(config.gfx_paths, child(target_layer, i), L"visible", 0, true);
        }
        if (number(native_canvas, L"numChildren") != native_marker_canvas_child_count)
        {
            return;
        }
        auto* source_layer = child(native_canvas, native_marker_layer_index);
        const auto sources = number(source_layer, L"numChildren");
        if (!std::isfinite(sources) || sources < 0 || sources > save_point_marker_limit)
        {
            return;
        }
        int shown = 0;
        if (visible(source_layer))
        {
            for (int i = 0; i < static_cast<int>(sources); ++i)
            {
                auto* source = child(source_layer, i);
                if (number(source, L"type") != save_point_marker_type ||
                    number(source, L"layer") != save_point_marker_layer || !visible(source))
                {
                    continue;
                }
                const auto alpha = number(source, L"alpha");
                if (!std::isfinite(alpha) || alpha <= 0)
                {
                    continue;
                }
                const auto point = map_overlay_canvas_point(
                    number(source, L"x"), number(source, L"y"), number(source_layer, L"x"),
                    number(source_layer, L"y"), number(source_layer, L"scaleX"),
                    number(source_layer, L"scaleY"), number(source_layer, L"rotation"));
                if (!point || !map_overlay_contains(*point, image_width, image_height))
                {
                    continue;
                }
                // addMarker constructs the game's resident, animated NaviIconSave.
                // Only this independent movie is changed; no assets are bundled.
                auto* marker =
                    shown < static_cast<int>(allocated)
                        ? child(target_layer, shown)
                        : gfx_numbers(config.gfx_paths, overlay, L"addMarker",
                                      {double(save_point_marker_type), double(shown + 1)});
                if (!is_uobject_valid_fast(marker))
                {
                    break;
                }
                gfx_set(config.gfx_paths, marker, L"x", point->x);
                gfx_set(config.gfx_paths, marker, L"y", point->y);
                gfx_set(config.gfx_paths, marker, L"scaleX", sw / reference_width / fit);
                gfx_set(config.gfx_paths, marker, L"scaleY", sw / reference_width / fit);
                gfx_set(config.gfx_paths, marker, L"alpha", std::clamp(alpha, 0.0, 1.0));
                gfx_set(config.gfx_paths, marker, L"visible", 1, true);
                ++shown;
            }
        }
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
        if (shown != last_save_point_count)
        {
            Output::send<LogLevel::Warning>(
                STR("[KH3APProbeCpp] Area map save points texture={} shown={} native={}\n"),
                texture_path, shown, sources);
        }
#endif
        last_save_point_count = shown;
    }
} // namespace kh3ap
