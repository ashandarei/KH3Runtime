# Gummiphone journal implementation

`ingredient_journal_service.cpp` owns the Ingredients/Adversaries/Gate Opener UI;
`ingredient_journal_map_service.cpp` owns source previews. Catalogs contain
metadata, not copies of game movies or textures. The [movie authoring guide](../../../../docs/MOVIE_SOURCES.md)
identifies editable sources, variables, compiler inputs and resource checks.

## Movie lifecycle and input

The runtime asynchronously loads `MobilePortal` at title startup and patches its
loaded movie before first construction through the OpenMovie pre-hook. Stock
size/fingerprint guards protect the replacement. The embedded patch adds the
original `kh3ap.APIngredientJournal` helper and a root-constructor call, retaining
stock code, symbols, graphics and timelines. Unknown movies are left untouched.

KH3 caches the stock definition before gameplay, so the custom page uses a
transient movie with a fresh cache identity. Hide its stock children and leave
the native page header visible. The Ingredients button retains its native
anchor/navigation and cooking icon; Adversaries retains its own header. Other
locked vanilla tiles keep their state. Suppress the stock cursor's root opacity
only while the custom Ingredients body is visible, restoring it on focus loss.

Native anchor/focus events select and publish the page on the game thread. Capture
UI proxies through `make_uobject_weak`, including missing UI serial allocation,
then resolve their GFx object. No home-screen custom input capture is installed.
The existing viewport owner routes custom navigation and retains ownership
through releases. Hide/disable the layer on focus loss and reuse it on later opens.
Custom pages and the extra settings menu share `core/input/menu_face_button`:
controller Confirm/Back follow `TresLSIGameActor::NeedToExchangeActionButton`
at the input boundary. Its true result means bottom/Cross is Confirm. Only
object references are cached; changing the native setting applies to the next
press. Keyboard, mouse and virtual actions retain their existing meanings.

Icons use the game's `tres.controls.Icon` and eight resident food textures,
preloaded/retained by the transient movie. Food-category art is shared; ingredient
names distinguish entries. Empty fresh-run state uses a no-argument clear call
because the native string bridge rejects empty arguments. The embedded patch
contains our helper/instructions; stock bytes come from the player's loaded movie.

## Source metadata and inactive previews

Friendly labels come from `minimap_reference.json`. Join sources to authored actor
placements, deduplicate by world/map and exclude rows without catalogued physical
pickups and the one-time gathering tutorial. Labels identify maps, not invented
subareas or exact player routes. Adversary locations are vanilla source evidence,
not guarantees about current randomized encounters.

Load food/enemy sublevels as **inactive UWorld assets** through the latent Kismet
loader. Never stream them into gameplay, run their actors, travel the pawn or
change story. If already streamed, read the existing level without changing its
state. Verify package identity against the level's outer world; its transient
`OwningWorld` can instead point at the current gameplay world. Reflected layout
and WorldSettings checks guard the serialized actor array.

Resolve authored XYs from root `RelativeLocation` and bounded parent transforms,
including attached actor groups. Omit socket attachments, absolute-transform
chains and invalid/nonfinite results. Cache only names and scalar positions;
retain no world/level/actor references, allowing normal garbage collection.

`NaviMapData` supplies sheet origins/scales. Validate texture dimensions, project
sources into sheets containing their XYs and prefer the overview with most dots.
L1/R1 cycles matching sheets; overlapping floors may share XY. Retain textures
only during preview, releasing them on Back/focus loss.

Index/deduplicate actor names once, permit up to four concurrent inactive sublevel
loads and cache 64 completed previews per game session. Cached reopen reuses dots
and payload; first visits still need the asset loader. Diagnostics report readiness,
lookup timing, asset-request counts and payload hashes. No authored coordinates
or map images are packaged as a substitute for live installed assets.

## Authoring and validation

Regenerate the embedded patch after ActionScript edits and run
`python authoring/kh3/movie_resources.py --check` from the runtime root. Normal
DLL builds reject stale pairs without requiring Java/JPEXS. Keep original and
reconstructed game movies as local fixtures; compile our helper independently.

The native `scene_attachment_test` covers translation, scale, rotation, nested
parents and nonfinite/overflow rejection. Live checks must cover empty and
populated journals, native header/cursor restoration, app switching, physical
navigation, source-map paging and attached actor groups. The shared gate-opener
policy must preserve receipt identity and cancellation without spending.
