# Map overlay implementation

`map_overlay_service.cpp` owns lifecycle/input and the public facade. Neighboring
components own area maps, minimaps, layout, markers, movie bindings and display
operations. `service_impl.h` owns shared state; `gfx_bindings.h` holds weak
movie/function identities. Keep scratch storage and metadata caches single-owned.
[UI movie authoring](../../../../../docs/MOVIE_SOURCES.md) covers source generation.

## Chest discovery and completion

`map_overlay_service.cpp` consumes the shared native chest-activation and
completion notifications. `markers.cpp` keeps a weak roster, requests one
catch-up scan on world change and removes expired actors without rescanning.
Both activation states are observed so inactive unopened boxes retain markers.
Queue overflow or off-thread arrivals request recovery scans. Closing/reopening
the UI retains the roster and queued arrivals.

`MapOverlayChestSchedule` separates event-dirty chest completion from the
five-second visible-map discovery pass for **emblems and ingredients**. There
is no periodic chest discovery/completion fallback. A roster change or completion
notification refreshes unopened state; unknown state, fake boxes and foreign-world
actors stay hidden. Position/projection work remains separate from discovery.

The native chest palette supplies treasure-to-Keyblade identities. Ownership is
shared by both maps and invalidated by grants, reconciliation epochs, seed/player
or pawn/menu/cutscene changes. Failed reads stay dim/hidden and retry on marker
updates. Icon selection does not alter interaction gates or physical palettes.

## Other markers and update boundaries

- Emblems reuse the existing tracker's weak loaded-component cache. The visible
  five-second reconciliation recovers resident actor variants/detectors, including
  boat attachments. Half-second snapshots query recorded state and positions;
  owner-level `OwningWorld` must match the current pawn. No global UObject scan.
- Ingredients discover current-world catalog actors at opening/texture changes
  and the visible five-second gate, then supply half-second value snapshots.
  Generated spawn-rate metadata filters markers independently of AP checks.
  Neither discovery nor snapshots run with collectible icons hidden on both maps.
- Save points mirror the native minimap's visible markers twice per second
  through its resident `addMarker` method, reusing at most 64 animated symbols.
- Moving map/minimap geometry uses the existing 16 ms gate. Large-map closure
  leaves enabled minimap icons active; when both layers are inactive, only the
  lightweight pawn/menu readiness checks remain.

## Projection and presentation

Match `NaviMapData` rows by `TextureAsset`, not row name: Caribbean row and texture
names differ. Recover native texture position/heading by reversing the minimap
canvas transform; compare projected Sora position against the stock marker and
suppress collectible icons on disagreement. Preserve stock keyblade orientation.
Clip to texture bounds; height chevrons use current Sora/marker z and a 600-unit
threshold. Minimap icons stay upright and smaller, without height chevrons.

Layers use priorities HudRight 120, area map 121, large-map icons 122 and minimap
icons 123. Set priorities before registration; modal menus remain above them.
Both overlay layers start hidden until texture dimensions, layout and markers
are ready. Clip from local (0, 0) with matching map-relative offsets. Verify
transforms on the following update, accept pixel rounding and bound repair retries.

Weak ancestor bindings and stable projection inputs are cached; player geometry,
visibility and transforms stay live. Owner, viewport, topology, layout, area,
texture and roster changes invalidate the appropriate cache. Same-movie
reparenting can take up to 100 ms to discover; bound-ancestor visibility is checked
each update. Position caches retain scalar values, not unguarded actor pointers.

L3/F8 closure hides and pauses the area movie while preserving decoded art,
texture and layout. Reopening refreshes Sora/collectibles before reveal. Menus,
lost gameplay readiness, pawn or area changes discard it. Save-point pools follow
that movie's lifetime. Temporary movies do not capture OS input.

## Movie and palette contract

The area movie reuses the live minimap's resident movie/texture and stock player
marker in a separate transient GFx player. The collectible movie is generated
from the runtime's ActionScript and original artwork, then embedded as RCDATA.
The DLL reads its mapped bytes once; it needs no external movie or icon pak.

`APMapChestIcon` calls `super()` and stops at construction. C++ controls projection,
zoom, clipping, palette and visibility; ActionScript installs no frame listeners.
The fixed compiler scaffold replaces both classes on generation so obsolete
constant-pool entries do not accumulate. Commit sources, generated movie and
metadata together; normal builds check hashes without invoking the authoring tools.

`MAP_COLORS` in `build_chest_icon.py` owns exact sRGB tints. Frame one is the
original chest; sorted Keyblade frames share shaded vector art. Select a stopped
frame only when appearance changes; frame identity participates in pool reuse.
The legend uses the same tint table, named sprites and vector glyphs. Eligible
rows compact without resizing text/swatches or moving the centered map viewport.
Its layer sits above markers; the battle-level display retains its own lifecycle.

## Validation

Run the runtime tests for projection/input/legend geometry, chest scheduling and
resource structure. Verify live travel, loaded/streamed boxes, opening completion,
unknown ownership, palette changes, different texture rows, viewport resizing and
both map layers. Native object-field caches retain validated property metadata
under weak class identity; values and reciprocal possession are still read live.
The large-map bulk display-info path must agree with member reads for position
and heading, including its unsupported-layout fallback.

Compare cold initialization with cached reopen using matched scenes. Cached
projection and fewer GFx calls alone do not establish an FPS improvement. Keep
native geometry timings distinct from GPU rendering and end-to-end input latency.
