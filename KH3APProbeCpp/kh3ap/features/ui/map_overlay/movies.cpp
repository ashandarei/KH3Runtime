#include "kh3ap/features/ui/map_overlay/service_impl.h"
#include "kh3ap/features/ui/map_overlay/map_chest_movie.h"
#include "kh3ap/core/objects/asset_load_helpers.h"
#include "kh3ap/core/gfx/gfx_reflection_helpers.h"
#include <DynamicOutput/DynamicOutput.hpp>
#include <Unreal/UObject.hpp>
#include <Unreal/UObjectGlobals.hpp>
#include <Unreal/Property/FArrayProperty.hpp>
#include <cmath>
#include <cstddef>
#include <string>
#include <vector>

namespace kh3ap
{
    using namespace RC;
    using namespace RC::Unreal;
    using namespace map_overlay_detail;
    using namespace map_overlay_settings;
    namespace map_overlay_detail
    {

        auto movie_bool(UObject* movie, const wchar_t* path, const wchar_t* parameter, bool value)
            -> void
        {
            if (!is_uobject_valid_fast(movie))
            {
                return;
            }
            auto* fn = owned_function(movie, path);
            if (!fn)
            {
                return;
            }
            std::vector<std::byte> params(fn->GetParmsSize());
            if (!write_bool_function_param(find_function_param_by_name(fn, parameter),
                                           params.data(), value))
            {
                return;
            }
            movie->ProcessEvent(fn, params.data());
        }
        auto open_movie(UObject* asset, const GfxProbeFunctionPaths& paths, uint8_t priority)
            -> UObject*
        {
            auto* self = UObjectGlobals::StaticFindObject<UObject*>(
                nullptr, nullptr, L"/Script/ScaleformUI.Default__GFxMoviePlayer");
            auto* fn = owned_function(self, L"/Script/ScaleformUI.GFxMoviePlayer:OpenMovie");
            if (!is_uobject_valid_fast(asset) || !is_uobject_valid_fast(self) || !fn)
            {
                return nullptr;
            }
            std::vector<std::byte> params(fn->GetParmsSize());
            if (!write_object_function_param(fn, params.data(), L"SwfMovie", asset))
            {
                return nullptr;
            }
            // Priority must be configured before OpenMovie registers the player with
            // Scaleform. Mutating a running player's property does not re-sort movies.
            FStaticConstructObjectParameters construction(self->GetClassPrivate(),
                                                          asset->GetOuterPrivate());
            construction.SetFlags = RF_Transient;
            auto* configured = UObjectGlobals::StaticConstructObject(construction);
            if (!is_uobject_valid_fast(configured) ||
                !write_integral_function_param(configured->GetPropertyByNameInChain(L"Priority"),
                                               configured, priority) ||
                !write_object_function_param(fn, params.data(), L"MoviePlayer", configured))
            {
                return nullptr;
            }
            write_bool_function_param(find_function_param_by_name(fn, L"DisplayWithHudOff"),
                                      params.data(), true);
            self->ProcessEvent(fn, params.data());
            auto* movie = try_read_function_object_param(fn, params.data(), L"ReturnValue");
            if (!is_uobject_valid_fast(movie))
            {
                return nullptr;
            }
            // OpenMovie constructs the root synchronously, but loadImage completes on a
            // later tick. Suppress the authored-size movie before this tick can render.
            // Fail closed if its root cannot be hidden rather than flash a partial UI.
            auto* root = try_read_reflected_object_property_value(movie, L"Root");
            if (!gfx_set(paths, root, L"visible", 0, true))
            {
                movie_bool(movie, L"/Script/ScaleformUI.GFxMoviePlayer:Close", L"Unload", true);
                return nullptr;
            }
            for (const auto* field :
                 {L"bAllowInput", L"bAllowFocus", L"bCaptureKeyboardInput", L"bCaptureMouseInput",
                  L"bCaptureGamepadInput", L"bCaptureTouchInput", L"bShowDefaultMouseCursor"})
            {
                write_bool_function_param(movie->GetPropertyByNameInChain(field), movie, false);
            }
            write_bool_function_param(movie->GetPropertyByNameInChain(L"bCloseOnLevelChange"),
                                      movie, true);
            movie_bool(movie, L"/Script/ScaleformUI.GFxMoviePlayer:SetMovieCanReceiveInput",
                       L"bCanReceiveInput", false);
            movie_bool(movie, L"/Script/ScaleformUI.GFxMoviePlayer:SetMovieCanReceiveFocus",
                       L"bCanReceiveFocus", false);
            if (auto* scale =
                    owned_function(movie, L"/Script/ScaleformUI.GFxMoviePlayer:SetViewScaleMode"))
            {
                std::vector<std::byte> mode(scale->GetParmsSize()); // NoScale = 0
                movie->ProcessEvent(scale, mode.data());
            }
            if (auto* align =
                    owned_function(movie, L"/Script/ScaleformUI.GFxMoviePlayer:SetAlignment"))
            {
                std::vector<std::byte> mode(align->GetParmsSize());
                if (write_integral_function_param(find_function_param_by_name(align, L"Align"),
                                                  mode.data(), 5))
                {
                    movie->ProcessEvent(align, mode.data());
                }
            }
            return movie;
        }

    } // namespace map_overlay_detail
    auto RuntimeMapOverlayService::Impl::chest_root(bool minimap) -> UObject*
    {
        return object((minimap ? minimap_player : chest_player).get(), L"Root");
    }
    auto RuntimeMapOverlayService::Impl::marker_root(bool minimap) -> UObject*
    {
        if (minimap)
        {
            auto* owner = chest_root(true);
            if (minimap_marker_owner.Get() != owner)
            {
                minimap_marker_owner = make_uobject_weak(owner);
                minimap_marker_layer = {};
                minimap_icon_bindings = {};
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
                minimap_position_audited = false;
#endif
            }
            auto* layer = minimap_marker_layer.Get();
            if (is_uobject_valid_fast(layer))
            {
                return layer;
            }
            layer = gfx_object(config.gfx_paths,
                               gfx_object(config.gfx_paths, owner, L"markerViewport"), L"markers");
            minimap_marker_layer = make_uobject_weak(layer);
            return layer;
        }
        return gfx_object(config.gfx_paths,
                          gfx_object(config.gfx_paths, chest_root(minimap), L"markerViewport"),
                          L"markers");
    }
    auto RuntimeMapOverlayService::Impl::minimap_icon(UObject* layer, MarkerKind kind, int index)
        -> UObject*
    {
        const bool emblem = kind == MarkerKind::Emblem, ingredient = kind == MarkerKind::Ingredient;
        const int limit = emblem       ? emblem_icon_limit
                          : ingredient ? ingredient_icon_limit
                                       : chest_icon_limit;
        const size_t offset = ingredient ? ingredient_binding_offset
                              : emblem   ? emblem_binding_offset
                                         : 0;
        if (index < 0 || index >= limit)
        {
            return nullptr;
        }
        auto& binding = minimap_icon_bindings[offset + index];
        auto* icon = binding.Get();
        if (is_uobject_valid_fast(icon))
        {
            return icon;
        }
        icon = gfx_object(config.gfx_paths, layer,
                          ((ingredient ? L"ingredient"
                            : emblem   ? L"emblem"
                                       : L"chest") +
                           std::to_wstring(index))
                              .c_str());
        binding = make_uobject_weak(icon);
        return icon;
    }
    auto RuntimeMapOverlayService::Impl::open_chest_movie(UObject* native_asset, bool minimap)
        -> void
    {
        if (chest_root(minimap))
        {
            return;
        }
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
        RuntimeHotPathProfileScope profile{L"map_overlay:icon_movie_open"};
#endif
        auto* asset =
            chest_asset_path.empty() ? nullptr : find_loaded_runtime_asset(chest_asset_path);
        if (!is_uobject_valid_fast(asset))
        {
            auto* klass = find_loaded_runtime_asset_class(L"/Script/ScaleformUI.SwfMovie");
            if (!is_uobject_valid_fast(klass))
            {
                return;
            }
            FStaticConstructObjectParameters params(klass, native_asset->GetOuterPrivate());
            params.SetFlags = static_cast<EObjectFlags>(RF_Transient | RF_Public);
            asset = UObjectGlobals::StaticConstructObject(params);
            if (!is_uobject_valid_fast(asset))
            {
                return;
            }
            auto* raw = asset->GetPropertyByNameInChain(L"RawData");
            if (!raw || raw->GetClass().GetName() != L"ArrayProperty" ||
                static_cast<FArrayProperty*>(raw)->GetInner()->GetElementSize() != 1)
            {
                return;
            }
            const auto movie = map_chest_movie_bytes();
            raw->ContainerPtrToValuePtr<RC::Unreal::TArray<uint8_t>>(asset)->Append(
                movie.data(), static_cast<int32_t>(movie.size()));
            write_bool_function_param(asset->GetPropertyByNameInChain(L"bUseGFxExport"), asset,
                                      true);
            // One mod-owned asset lives for this service. It contains only the
            // supplied icon and our empty sprite pool, never a copied KH3 movie.
            asset->SetRootSet();
            chest_asset_path = asset->GetPathName();
        }
        if (auto* player = open_movie(asset, config.gfx_paths,
                                      minimap ? minimap_icon_priority : icon_movie_priority))
        {
            if (!(minimap ? minimap_player : chest_player).remember(player))
            {
                movie_bool(player, L"/Script/ScaleformUI.GFxMoviePlayer:Close", L"Unload", true);
                return;
            }
            auto& path = minimap ? minimap_movie_path : chest_movie_path;
            path = player->GetPathName();
            if (minimap)
            {
                minimap_presented = false;
            }
            Output::send<LogLevel::Warning>(
                STR("[KH3APProbeCpp] Chest icon movie player={} asset={} root={}\n"), path,
                asset->GetPathName(), object(player, L"Root") != nullptr);
        }
    }
    auto RuntimeMapOverlayService::Impl::clear_chest_icons(bool minimap) -> void
    {
        auto* icons = marker_root(minimap);
        auto& chest_count = minimap ? minimap_chests : displayed_icons;
        auto& emblem_count = minimap ? minimap_emblems : displayed_emblems;
        auto& ingredient_count = minimap ? minimap_ingredients : displayed_ingredients;
        for (int i = 0; i < chest_count; ++i)
        {
            gfx_set(config.gfx_paths,
                    gfx_object(config.gfx_paths, icons, (L"chest" + std::to_wstring(i)).c_str()),
                    L"alpha", 0);
        }
        if (emblem_count)
        {
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
            RuntimeHotPathProfileScope profile{L"map_overlay:emblem_hide"};
#endif
            for (int i = 0; i < emblem_count; ++i)
            {
                gfx_set(
                    config.gfx_paths,
                    gfx_object(config.gfx_paths, icons, (L"emblem" + std::to_wstring(i)).c_str()),
                    L"alpha", 0);
            }
        }
        chest_count = 0;
        emblem_count = 0;
        for (int i = 0; i < ingredient_count; ++i)
        {
            gfx_set(
                config.gfx_paths,
                gfx_object(config.gfx_paths, icons, (L"ingredient" + std::to_wstring(i)).c_str()),
                L"alpha", 0);
        }
        ingredient_count = 0;
        if (minimap)
        {
            rendered_minimap.clear();
        }
    }
    auto RuntimeMapOverlayService::Impl::movie() -> UObject*
    {
        return area_player.get();
    }
    auto RuntimeMapOverlayService::Impl::show_battle_level(bool visible) -> void
    {
        if (battle_level_presented == visible)
        {
            return;
        }
        const bool applied =
            config.set_battle_level_visible && config.set_battle_level_visible(visible);
        // Retry a show if the world's UMG widget has not finished bootstrapping.
        // A discarded world has no remaining display to hide.
        battle_level_presented = visible && applied;
    }
    auto RuntimeMapOverlayService::Impl::restore_native_minimap() -> void
    {
        // Re-resolve temporary GFx wrappers through their weak native HUD owner.
        // A GetMember wrapper must not own restoration across frames or GC.
        auto* hud = suppressed_minimap_hud.Get();
        auto* right = gfx_object(config.gfx_paths, root(hud), L"hudRight");
        auto* clip = gfx_object(config.gfx_paths, right, L"naviMap");
        if (is_uobject_valid_fast(clip))
        {
            gfx_set(config.gfx_paths, clip, L"alpha", native_minimap_alpha);
        }
        suppressed_minimap_hud = {};
    }
    auto RuntimeMapOverlayService::Impl::suppress_native_minimap(UObject* p) -> void
    {
        const auto [hud, nav] = live_parts(p);
        auto* right = gfx_object(config.gfx_paths, root(hud), L"hudRight");
        auto* clip = gfx_object(config.gfx_paths, right, L"naviMap");
        if (!is_uobject_valid_fast(clip))
        {
            return;
        }
        if (suppressed_minimap_hud.Get() != hud)
        {
            restore_native_minimap();
            const auto alpha = gfx_number(config.gfx_paths, clip, L"alpha");
            if (!alpha || !std::isfinite(*alpha))
            {
                return;
            }
            native_minimap_alpha = *alpha;
            suppressed_minimap_hud = make_uobject_weak(hud);
        }
        // Opacity hides only this map, preserving authored visibility/fades
        // and the rest of HudRight. Retain its original opacity for close/travel.
        gfx_set(config.gfx_paths, clip, L"alpha", 0.);
        hide_minimap();
    }
    auto RuntimeMapOverlayService::Impl::suspend() -> void
    {
        triggers.cancel_pending();
        show_battle_level(false);
        restore_native_minimap();
        // Keep the decoded movies, resident image and layout for the next tap.
        // Pause hidden timelines so the retained UI does not keep animating.
        gfx_set(config.gfx_paths, object(movie(), L"Root"), L"visible", 0, true);
        gfx_set(config.gfx_paths, chest_root(), L"visible", 0, true);
        movie_bool(movie(), L"/Script/ScaleformUI.GFxMoviePlayer:SetPause", L"bPausePlayback",
                   true);
        movie_bool(chest_player.get(), L"/Script/ScaleformUI.GFxMoviePlayer:SetPause",
                   L"bPausePlayback", true);
        opened = false;
        presented = false;
    }
    auto RuntimeMapOverlayService::Impl::hide(UObject*) -> void
    {
        triggers.cancel_pending();
        restore_native_minimap();
        keyblade_ownership.clear();
        legend_rows.clear();
        layout_legend_rows.clear();
        owned_legend_rows.clear();
        rendered_owned_legend_rows.clear();
        legend_style_dirty = true;
        show_battle_level(false);
        if (auto* player = movie())
        {
            movie_bool(player, L"/Script/ScaleformUI.GFxMoviePlayer:Close", L"Unload", true);
        }
        movie_bool(chest_player.get(), L"/Script/ScaleformUI.GFxMoviePlayer:Close", L"Unload",
                   true);
        movie_bool(minimap_player.get(), L"/Script/ScaleformUI.GFxMoviePlayer:Close", L"Unload",
                   true);
        area_player.reset();
        chest_player.reset();
        minimap_player.reset();
        minimap_movie_path.clear();
        minimap_chests = minimap_emblems = minimap_ingredients = 0;
        chest_movie_path.clear();
        displayed_icons = 0;
        displayed_emblems = 0;
        displayed_ingredients = 0;
        movie_path.clear();
        texture_path.clear();
        area.reset();
        next_chests = {};
        last_chest_count = -1;
        last_emblem_count = -1;
        last_ingredient_count = -1;
        next_save_points = {};
        last_save_point_count = -1;
        layout_width = layout_height = image_width = image_height = 0;
        map_x = map_y = layout_zoom = 0;
        layout_legend = false;
        layout_confirmation_pending = false;
        layout_confirmation_attempts = 0;
        next_layout_probe = {};
        rendered_player.reset();
        opened = false;
        presented = false;
        // Keep the weak chest roster and queued arrivals while the UI is hidden.
        rendered_chests.clear();
        chest_schedule = {};
        chest_layout_dirty = true;
        minimap_presented = false;
        minimap_texture_path.clear();
        minimap_texture = {};
        minimap_area.reset();
        minimap_marker_owner = {};
        minimap_marker_layer = {};
        minimap_icon_bindings = {};
        minimap_layout_cache.clear();
        next_minimap_markers = {};
        minimap_markers.clear();
        rendered_minimap.clear();
        last_minimap_count = -1;
    }
} // namespace kh3ap
