#include "kh3ap/features/ui/map_overlay/service_impl.h"
#include "kh3ap/core/gfx/gfx_reflection_helpers.h"
#include <DynamicOutput/DynamicOutput.hpp>
#include <algorithm>
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

    auto RuntimeMapOverlayService::Impl::clip_viewport(UObject* target, double x, double y,
                                                       double width, double height) -> bool
    {
        // getBounds returns a native AS3 Rectangle without constructing game
        // assets. scrollRect copies it, so no raw GFx object survives this call.
        auto* rect = gfx_object_argument(config.gfx_paths, target, L"getBounds", target);
        return rect && gfx_set(config.gfx_paths, rect, L"x", x) &&
               gfx_set(config.gfx_paths, rect, L"y", y) &&
               gfx_set(config.gfx_paths, rect, L"width", width) &&
               gfx_set(config.gfx_paths, rect, L"height", height) &&
               try_gfx_set_member_value_object(config.gfx_paths, target, L"scrollRect", rect);
    }
    auto RuntimeMapOverlayService::Impl::confirm_layout(UObject* overlay,
                                                        const MapOverlayLayout& layout) -> bool
    {
        auto get = [&](UObject* owner, const wchar_t* name)
        {
            return gfx_object(config.gfx_paths, owner, name);
        };
        auto matches = [&](UObject* owner, const wchar_t* name, double expected, bool scale = false)
        {
            const auto actual = gfx_number(config.gfx_paths, owner, name);
            const bool matched = actual && map_overlay_layout_matches(*actual, expected, scale);
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
            if (!matched)
                Output::send<LogLevel::Warning>(STR("[KH3APProbeCpp] Area map layout settling "
                                                    "member={} expected={} actual={}\n"),
                                                name, expected, actual.value_or(NAN));
#endif
            return matched;
        };
        auto at = [&](UObject* owner, double x, double y)
        {
            return matches(owner, L"x", x) && matches(owner, L"y", y);
        };
        auto clipped = [&](UObject* owner, double x, double y)
        {
            auto* rect = get(owner, L"scrollRect");
            return at(rect, x, y) && matches(rect, L"width", layout.view_width) &&
                   matches(rect, L"height", layout.view_height);
        };
        auto* viewport = get(chest_root(), L"markerViewport");
        auto* canvas = get(overlay, L"mapCanvas");
        return at(overlay, layout.view_x, layout.view_y) && clipped(overlay, 0, 0) &&
               at(get(overlay, L"map"), map_x - layout.view_x, map_y - layout.view_y) &&
               matches(canvas, L"scaleX", layout.fit, true) &&
               matches(canvas, L"scaleY", layout.fit, true) &&
               at(viewport, layout.view_x, layout.view_y) && clipped(viewport, 0, 0) &&
               at(marker_root(), map_x - layout.view_x, map_y - layout.view_y);
    }
    auto RuntimeMapOverlayService::Impl::draw_keybinds(UObject* p, double sw, double sh,
                                                       bool relayout) -> void
    {
        const bool enabled = !config.keybinds_enabled || config.keybinds_enabled();
        bool locked = false;
        if (enabled && area)
        {
            const auto state = reconciliation::view();
            const auto palette = state->chests;
            if (map_chest_palette_matches(palette.get(), GetCurrentProcessId(), state->seed,
                                          state->slot))
            {
                sync_keyblade_ownership(*state);
                for (const auto& chest : chests)
                {
                    if (!chest.unopened || !chest.located || !chest.actor.Get())
                    {
                        continue;
                    }
                    const auto point =
                        map_overlay_world_point(*area, chest.location[0], chest.location[1]);
                    if (point && map_overlay_contains(*point, image_width, image_height) &&
                        map_chest_is_locked(palette.get(), chest.treasure,
                                            [&](const std::wstring& token)
                                            {
                                                return owns_keyblade(p, token);
                                            }))
                    {
                        locked = true;
                        break;
                    }
                }
            }
        }
        const int lines = enabled ? (locked ? 3 : 2) : 0;
        if (!relayout && lines == keybind_hint_lines)
        {
            return;
        }
        auto* hints = gfx_object(config.gfx_paths, chest_root(), L"keybindHints");
        if (!hints)
        {
            return;
        }
        const double scale = std::min(sw / reference_width, sh / reference_height);
        const bool applied =
            gfx_set(config.gfx_paths, hints, L"visible", enabled, true) &&
            gfx_set(config.gfx_paths, hints, L"alpha", enabled ? 1. : 0.) &&
            // Match W_ProofUI's battle-level anchor (.12,.09), position
            // (16,12), and 32px header, then leave a 6px gap below it.
            gfx_set(config.gfx_paths, hints, L"x",
                    sw * keybind_anchor_x + keybind_left_padding * scale) &&
            gfx_set(config.gfx_paths, hints, L"y",
                    sh * keybind_anchor_y + keybind_top_offset * scale) &&
            gfx_set(config.gfx_paths, hints, L"scaleX", scale) &&
            gfx_set(config.gfx_paths, hints, L"scaleY", scale) &&
            gfx_set(config.gfx_paths, gfx_object(config.gfx_paths, hints, L"lockedChests"),
                    L"visible", locked, true);
        if (applied)
        {
            keybind_hint_lines = lines;
        }
    }
} // namespace kh3ap
