#pragma once
#include "kh3ap/features/ui/map_overlay/map_overlay_service.h"
#include "kh3ap/features/ui/map_overlay/support.h"
#include "kh3ap/features/ui/map_overlay/settings.h"
#include "kh3ap/features/ui/map_overlay/gfx_bindings.h"
#include "kh3ap/features/ui/map_overlay/map_overlay_chest_schedule.h"
#include "kh3ap/features/ui/map_overlay/map_chest_policy.h"
#include "kh3ap/features/ui/map_overlay/map_marker_position_cache.h"
#include "kh3ap/features/ui/map_overlay/minimap_ancestry_cache.h"
#include "kh3ap/features/ui/map_overlay/minimap_projection_cache.h"
#include "kh3ap/core/events/keyblade_inventory_events.h"
#include "kh3ap/features/treasure/chest_discovery_queue.h"
#include "kh3ap/bridge/rewards/reconciliation.h"
#include <Unreal/FWeakObjectPtr.hpp>
#include <array>
#include <chrono>
#include <cstring>
#include <optional>
#include <utility>
#include <vector>
namespace kh3ap
{
    using namespace RC::Unreal;
    using namespace map_overlay_detail;
    struct RuntimeMapOverlayService::Impl
    {

        RuntimeMapOverlayConfig config;
        MapOverlayInput input;
        MapOverlayZoom zoom;
        MapTriggerInput triggers;
        uint32_t last_icon_options{map_icon_defaults};
        FWeakObjectPtr pawn;
        FWeakObjectPtr hold_pawn;
        bool eligible{}, pending_toggle{}, opened{}, presented{}, logged{};
        bool battle_level_presented{};
        FWeakObjectPtr suppressed_minimap_hud;
        double native_minimap_alpha{1.};
        int last_status{-1};
        std::wstring movie_path, texture_path;
        std::wstring chest_movie_path, minimap_movie_path;
        MapMovieBinding area_player, chest_player, minimap_player;
        std::wstring chest_asset_path;
        std::chrono::steady_clock::time_point next_tick{};
        std::chrono::steady_clock::time_point last_layout{};
        std::chrono::steady_clock::time_point next_layout_probe{};
        std::optional<std::array<double, 3>> rendered_player;
        double layout_width{}, layout_height{}, image_width{}, image_height{};
        double map_x{}, map_y{}, layout_zoom{};
        bool layout_legend{};
        int keybind_hint_lines{-1};
        std::vector<size_t> legend_rows, layout_legend_rows;
        std::vector<size_t> owned_legend_rows, rendered_owned_legend_rows;
        bool legend_style_dirty{true};
        RuntimeKeybladeOwnershipCache keyblade_ownership;
        int64_t ownership_epoch{-1};
        std::wstring ownership_seed;
        int64_t ownership_slot{};
        bool layout_confirmation_pending{};
        unsigned layout_confirmation_attempts{};
        std::optional<MapOverlayArea> area;
        std::chrono::steady_clock::time_point next_chests{};
        std::chrono::steady_clock::time_point next_save_points{};
        int last_save_point_count{-1};
        int last_chest_count{-1};
        int last_emblem_count{-1};
        int last_ingredient_count{-1};
        int displayed_icons{};
        int displayed_emblems{};
        int displayed_ingredients{};
        int minimap_chests{}, minimap_emblems{}, minimap_ingredients{};
        struct Chest
        {
            FWeakObjectPtr actor;
            std::wstring treasure;
            std::array<float, 3> location{};
            bool located{}, unopened{};
        };
        enum class MarkerKind
        {
            Chest,
            Emblem,
            Ingredient
        };
        struct ChestMarker
        {
            double x{}, y{};
            int height{};
            MarkerKind kind{MarkerKind::Chest};
            int frame{1};
            double alpha{1.0};
            bool operator==(const ChestMarker&) const = default;
        };
        std::vector<Chest> chests;
        struct SameWeakIdentity
        {
            bool operator()(const FWeakObjectPtr& a, const FWeakObjectPtr& b) const
            {
                static_assert(sizeof(FWeakObjectPtr) == 2 * sizeof(int32_t));
                return std::memcmp(&a, &b, sizeof(a)) == 0;
            }
        };
        ChestDiscoveryQueue<FWeakObjectPtr, map_overlay_settings::chest_discovery_limit,
                            SameWeakIdentity>
            chest_discovery;
        FWeakObjectPtr chest_world;
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
        uint64_t chest_discovery_scans{}, chest_event_batches{};
#endif
        std::vector<ChestMarker> rendered_chests, large_marker_scratch;
        struct LandmarkPositions
        {
            MapMarkerPositionCache emblems, ingredients;
        };
        LandmarkPositions large_positions, minimap_positions;
        std::vector<RuntimeLuckyEmblemMarker> emblem_snapshot;
        std::vector<RuntimeIngredientMarker> ingredient_snapshot;
        MapOverlayChestSchedule chest_schedule;
        bool chest_layout_dirty{true};
        bool minimap_presented{};
        std::wstring minimap_texture_path;
        std::optional<MapOverlayArea> minimap_area;
        std::chrono::steady_clock::time_point next_minimap_markers{};
        std::vector<ChestMarker> minimap_markers, rendered_minimap, minimap_scratch;
        double minimap_art_size{};
        int last_minimap_count{-1};
        FWeakObjectPtr minimap_marker_owner, minimap_marker_layer, minimap_texture;
        std::array<FWeakObjectPtr, map_overlay_settings::minimap_icon_binding_count>
            minimap_icon_bindings;
        MinimapLayoutCache minimap_layout_cache;
        MinimapProjectionCache minimap_projection_cache;
        std::vector<double> minimap_frame_inputs, minimap_parent_scratch, minimap_frame_scratch;
        FWeakObjectPtr minimap_input_host;
        MinimapAncestryCache minimap_hud_ancestry, minimap_canvas_ancestry;
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
        std::chrono::steady_clock::time_point next_minimap_audit{};
        bool minimap_position_audited{};
#endif

        explicit Impl(RuntimeMapOverlayConfig value) : config(std::move(value))
        {
        }

        auto enabled() const -> bool;

        auto icon_options() const -> uint32_t;

        auto icon_enabled(MapIconOption option) const -> bool;

        auto sync_keyblade_ownership(const reconciliation::View& state) -> void;

        auto owns_keyblade(UObject* p, const std::wstring& token) -> bool;

        auto chest_root(bool minimap = false) -> UObject*;

        auto marker_root(bool minimap = false) -> UObject*;

        auto minimap_icon(UObject* layer, MarkerKind kind, int index) -> UObject*;

        auto clip_viewport(UObject* target, double x, double y, double width, double height)
            -> bool;

        auto confirm_layout(UObject* overlay, const MapOverlayLayout& layout) -> bool;

        auto open_chest_movie(UObject* native_asset, bool minimap = false) -> void;

        auto clear_chest_icons(bool minimap = false) -> void;

        auto movie() -> UObject*;

        auto show_battle_level(bool visible) -> void;

        auto restore_native_minimap() -> void;

        auto suppress_native_minimap(UObject* p) -> void;

        auto suspend() -> void;

        auto hide(UObject*) -> void;

        auto collect_markers(UObject* p, const std::optional<std::array<float, 3>>& sora,
                             const std::optional<MapOverlayArea>& area, double image_width,
                             double image_height, double fit,
                             std::chrono::steady_clock::time_point now,
                             std::vector<ChestMarker>& markers, LandmarkPositions& positions)
            -> void;

        auto draw_keybinds(UObject* p, double sw, double sh, bool relayout) -> void;

        auto draw_chests(UObject* p, UObject* overlay, const MapOverlayPoint& pc, double fit,
                         double sw, std::chrono::steady_clock::time_point now) -> void;

        auto draw_save_points(UObject* native_canvas, UObject* overlay, double fit, double sw,
                              std::chrono::steady_clock::time_point now) -> void;

        auto hide_minimap() -> void;

        auto tick_minimap(UObject* p, std::chrono::steady_clock::time_point now) -> void;

        auto tick() -> void;
    };
} // namespace kh3ap
