# Readable KH3 movie sources

Start here when changing the UI movies embedded in the KH3 DLL. Edit the
readable source, regenerate its movie data, then build the DLL. The movie and
patch binaries are generated assets embedded as Windows resources; KH3 runs
the compiled movie bytes. These source files and this guide add no runtime work.

## Find the source

| UI | Readable source | Generated DLL data |
| --- | --- | --- |
| Shop AP names, icons, colors, Forge navigation | [APShopRuntime.as](../authoring/kh3/shop_ui/APShopRuntime.as) | [EGS patch binary](../KH3APProbeCpp/resources/shop_movie_patch_egs.bin), [Steam patch binary](../KH3APProbeCpp/resources/shop_movie_patch_steam.bin) |
| Gummiphone Ingredients, Adversaries, Gate Opener and Chests pages | [APIngredientJournal.as](../authoring/kh3/ingredient_journal/APIngredientJournal.as) | [Journal patch binary](../KH3APProbeCpp/resources/ingredient_journal_patch.bin) |
| Map movie script | [APMapChestIcons.as](../authoring/kh3/map_overlay/APMapChestIcons.as), [APMapChestIcon.as](../authoring/kh3/map_overlay/APMapChestIcon.as) | [Compiled map movie](../KH3APProbeCpp/resources/map_chest_movie.swf) |
| Now playing title and scrolling | [Music movie source](../authoring/kh3/now_playing/APNowPlaying.as), [generator](../authoring/kh3/now_playing/build_movie.py) | [Compiled music movie](../KH3APProbeCpp/resources/now_playing_movie.swf) |
| Map marker art, legend, keybind labels | [build_chest_icon.py](../authoring/kh3/map_overlay/build_chest_icon.py) and its PNG inputs | Same compiled movie; [readable C++ dimensions](../KH3APProbeCpp/kh3ap/features/ui/map_overlay/map_chest_movie.h) |

The shop and journal use ActionScript 3 (`.as`). Their generators compile our
helper and encode the changes needed to extend the installed game movie. Game
code and art continue to come from that movie.

The shop and journal use `.bin` patch resources because they extend stock game
movies. Their small C++ headers retain sizes, fingerprints, source hashes and
cached resource accessors. The stock-input and reconstructed-output fingerprint
checks still run before an asset is replaced. Only the compiled AP helper and
audited patch instructions ship; the game's full movies are not bundled.

The map movie script is authored in the two `.as` files and compiled by
Java/JPEXS. `APMapChestIcons` is the root; `APMapChestIcon` calls `super()` and
`this.stop()` in its constructor. The native renderer selects the chest's
palette frame. Python builds artwork and placements ahead of time from the
PNGs and combines them with the compiled script. The DLL embeds the result as
a Windows binary resource, so its C++ header contains no movie byte array.

## Shop variables

| Name | Meaning |
| --- | --- |
| `STATE` | Property name used to keep our cached state on each shop row. |
| `ICON_ROOT` | Unreal asset directory containing the AP item icons. |
| `USEFUL_COLOR`, `PROGRESSION_COLOR` | Item-name colors, encoded as RGB hex values. |
| `markedName`, `marker`, `gameMarker` | Original item name and its invisible classification/game markers. Keep the native producer and authored marker table consistent. |
| `name` | Displayed item name after removing those markers. |
| `state.icons` | Previously created icon objects, keyed by icon name. |
| `state.icon`, `state.path` | Currently selected icon and its texture path. |
| `state.colorCaptured`, `state.defaultColor` | Whether the original row text color has been saved, and that saved color. |
| `category` | Vanilla category glyph, hidden when a custom icon is visible. |

`attachRow()` installs the row helper; `refreshRow()` updates its text and icon.
`attachForge()` hides the second command and updates the remaining navigation
links. Names such as `cmd1`, `cmd2`, `txName` and `txIcon` belong to the game's UI
and must still match its movie.

## Journal variables

| Name | Meaning |
| --- | --- |
| `active`, `ready`, `focused` | Whether our journal is open, its catalog is loaded, and the host has focus. |
| `adversaries`, `gates`, `chests` | Alternate journal modes selected by native code through `setJournal()`; Ingredients is the default. |
| `entries`, `visibleEntries` | Full catalog and the entries passing the current world filter. |
| `collected` | Recorded ingredient, enemy and chest completion values supplied by native code. |
| `selected`, `worldIndex`, `selectedSource` | Zero-based indexes for the selected entry, world filter and source row. |
| `selectedIngredient`, `selectedWorld` | Selection exposed to the native service. |
| `paneMode` | Navigation pane: `0` grid, `1` source list, `2` map. |
| `gridScroll`, `sourceScroll` | Scroll offsets in the movie's design coordinates. |
| `mapRequestKey`, `mapRequestSerial` | Selected source-map request and its change counter for native code. |
| `mapState`, `mapTexture`, `mapMarkerCount`, `mapPageCount` | Map loading/display state, texture path, marker count and sheet count. |
| `mapSheets`, `mapIndex` | Available map sheets and the selected sheet's zero-based index. |
| `COLUMNS`, `columns` | Ingredients grid width (`8`) and the active grid width (`4` for Adversaries and Chests). |
| `font` | Game font used by the authored text fields. |

Start with the constructor for layout, `loadCatalog()` for catalog input,
`setCollected()` for completion updates, `navigateInternal()` for controls,
`rebuild()` for filtering/layout, and `paint()` for selection appearance.
`showSources()` builds the source panel; `openMap()` and `loadMap()` handle its
map page. Native code reads several public fields, so renaming them also requires
updating [ingredient_journal_service.cpp](../KH3APProbeCpp/kh3ap/features/journal/ingredient_journal_service.cpp).

The chest journal marker shares `map_overlay/chest.png` and its icon preparation
with the minimap/map. Its build fills `CHEST_ART` with the same colored
rectangles; `chestMarker()` draws and caches that artwork when a preview opens.
The PNG and encoder hashes are checked with the journal patch, so changing the
shared artwork requires regenerating the journal as well.

Chest entries and source paths are generated from the runtime's versioned
`data/runtime-inputs.json` and `data/chest-journal-sources.json` by
`scripts/generate_catalogs.py` into one `chest_journal_catalog.h`: each entry
contains its labels and actor paths. The sources JSON is the asset-export input;
the catalog is generated and is not maintained separately.
Only chests registered for the active seed appear.
Imported 0.2 entries require the seed option, and their maps also require
Oblivion. Selecting the location text opens the preview; descriptions remain
plain text when no map is available. Melody chest positions come from the
current seed's assignments and are refreshed whenever their preview opens.

## Map variables and named elements

| Name | Meaning |
| --- | --- |
| `MAP_COLORS` | RGB hex tint for each chest Keyblade. |
| `LEGEND_WIDTH`, `LEGEND_HEIGHT` | Legend dimensions in design pixels. |
| `KEYBIND_WIDTH`, `KEYBIND_ROW_HEIGHT`, `KEYBIND_LABELS` | Hint row size and displayed text. |
| `keyblade_colors()` | Validates Keyblade identities and returns the colors in stable order. |
| `legend_layers()`, `keybind_layers()` | Readable text, colors and layout used to author the artwork. Artwork is rendered at twice the design resolution. |
| `APMapChestIcons()` | ActionScript root constructor; the timeline supplies the named marker containers and panels. |
| `APMapChestIcon()` | ActionScript chest constructor; stops its palette timeline immediately. |
| `compile_document_classes()` | Compiles both `.as` classes during authoring. |
| `markerViewport.markers` | Container holding the movable/clipped marker pools. |
| `chest0`–`chest127`, `emblem0`–`emblem89`, `ingredient0`–`ingredient127` | Initially hidden marker instances controlled by native code. |
| `keybladeLegend.panel`, `.title`, `.row0`–`.row14` | Legend panel, title and individual Keyblade rows. |
| `keybindHints.zoomOut`, `.zoomIn`, `.lockedChests` | Individual hint rows. |

SWF placement coordinates use **20 twips per design pixel**. `rect()` and
`matrix()` encode those coordinates. Legend artwork uses a half-scale placement
because it is rendered at twice the design resolution. The icon PNGs are
converted into vector fills because KH3 does not decode embedded bitmaps.

Movie art and scripting live here; current-world positions, zoom and visibility
policy live in [map_overlay_service.cpp](../KH3APProbeCpp/kh3ap/features/ui/map_overlay/map_overlay_service.cpp)
and [minimap_context_service.cpp](../KH3APProbeCpp/kh3ap/features/ui/map_overlay/minimap_context_service.cpp).

## Generate and verify

Regenerating the map needs Python, Pillow and Java/JPEXS, using the same compiler
as the shop and journal. Set `JAVA_HOME` and `FFDEC_JAR` to your local compiler
installation, then run from the project root:

```powershell
python -m pip install -r requirements-authoring.txt
python authoring/kh3/map_overlay/build_chest_icon.py --java "$env:JAVA_HOME/bin/java.exe" --ffdec "$env:FFDEC_JAR"
python authoring/kh3/now_playing/build_movie.py --java "$env:JAVA_HOME/bin/java.exe" --ffdec "$env:FFDEC_JAR"
```

For the shop and journal, provide the matching original movie from your licensed
game installation to the [shop generator](../authoring/kh3/shop_ui/build_runtime_patch.py)
or [journal generator](../authoring/kh3/ingredient_journal/build_runtime_patch.py).
Their `--help` describes the required input, compiler and output arguments.
Each shop variant must be generated from its matching stock movie. Do not check
the stock movie or reconstructed full movie into Git. EGS is the default local
test platform; Steam authoring requires an explicit Steam task.

Generation is an explicit authoring step. Normal C++ builds consume the
checked-in `.swf`/`.bin` resources and their small generated headers. They
validate source and binary hashes and reject stale `.as` files; they do not
compile ActionScript automatically. Normal DLL builds need neither Java/JPEXS
nor Pillow. All resources are read from the mapped DLL and cached; the game
does not need external movie or patch files.

```powershell
python authoring/kh3/map_overlay/build_chest_icon.py --check
python authoring/kh3/movie_resources.py --check
```
After a behavior change, build/install the DLL with the repository's
`scripts/build.ps1 -Install` helper with KH3 closed. An
explanation or source-only refactor that produces identical compiled bytes does
not require a DLL rebuild.
