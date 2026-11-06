# KH3AP runtime source layout

Start with the directory that owns the behavior. This guide maps ownership and
shared lifecycle rules; feature references below describe their detailed contracts.

## Source map

| Area | Responsibility and entry points |
| --- | --- |
| `app/` | UE4SS lifecycle, composition, ordered updates and top-level state. |
| `bridge/` | `catalog/` lookups, `checks/` locations, `rewards/` receipts/reconciliation, `serialization/` codecs, `session/` identity and `transport/` IPC/persistence. |
| `core/` | Shared events, input, memory, objects, platform, settings and UI primitives. `reflection/` and `gfx/` own common Unreal/Scaleform access. |
| `diagnostics/` | Ability inspection, autosave probes, grant/retry instrumentation, output and profiling. |
| `hooks/` | Native hook registration/lifetime, blocking UI, frame waits, inventory, lucky marks, memory and patches. |
| `features/enemies/` | Enemy/boss substitution, loaded entities and encounter repairs. |
| `features/grants/` | Delivery, abilities, VBonuses, Keyblade levels, messages, events and battle levels. |
| `features/player/` | `context/`, `objects/`, `state/`, `keyblades/`, `controller_abilities/`, `characters/`, `movement/`, `combat/` and `emblems/`. |
| `features/story/` | Bootstrap, cutscenes, progression, ReMind, world fixes, minigames and save points. |
| `features/world/` | Portals, startup, Garden return, UI control and space navigation. |
| `features/ui/` | GoA menu models/services, settings, sounds, map overlays and Keyblade-level UI. |
| `features/ingredients/`, `features/journal/` | Field collection/checkpoints, journal presentation and source-map previews. |
| `features/treasure/`, `features/moogle_shop/` | Chest discovery/rewards, music-chest interaction and shop purchase checks. |
| `features/music/` | `audio/` decoding/playback, `events/` game hooks, `shuffle/` selection/form themes and `now_playing/` presentation. |
| `features/party/`, `features/colors/`, `features/tutorials/` | Companion replacement/policy, material palettes and tutorial suppression. |
| `features/kh02/` | Imported KH0.2 runtime, source preparation, story and player behavior. |

## Adding or changing a component

Keep a feature's header and implementation together. Add each `.cpp` explicitly
to the target's CMake source lists; preserve diagnostic-only build exclusion.
Use module-qualified includes rooted at `KH3APProbeCpp/`, including local headers:

```cpp
#include "kh3ap/features/player/context/context_store.h"
```

Do not expose individual module directories as include roots. External roots
come from their CMake targets, with the existing stb C-source exception. Tests
use the same project root and qualified engine-stub headers. Forward-declare
objects/services when pointer/reference declarations suffice; include concrete
dependencies in the implementation. Qualified paths expose dependencies but do
not enforce a layer hierarchy.

Name internal sources for their responsibility without redundant `runtime_`
prefixes; retain the term when it distinguishes live behavior from generation or
installation. Keep generated catalogs reproducible from this project's inputs.

## Composition and lifetime

`app/composition.cpp` owns startup, initialization order and map resets. Private
`app/composition/service_graph.h` lists value-owned services in construction
order; destruction reverses it. Construct non-movable services in their final
storage and retain forward-callback guards until the graph is ready.

`*_wiring.cpp` groups configuration; `update_wiring.cpp` defines ordered frame
callbacks and `update_trace.h` their scopes. `*_bindings.cpp` supplies paths,
context, diagnostics and reward routing. Observer callback state can outlive the
composition owner and retains process lifetime. Public callers use `composition.h`.

Prefer existing events/revisions; bounded pending work may tick until completion.
Preserve callback order, early returns, cancellation and teardown when splitting
services. Validate UObjects with `is_uobject_valid_fast`; retain weak identities
across ticks/world changes. Uninstall hooks before clearing their owner. Keep
private state, mutexes and shared scratch/cache definitions single-owned.

## Implementation boundaries

- Enemy remapping keeps installation/hooks in `enemy_remap_service.cpp`, pure
  manifest validation in `remap_catalog.cpp` and entity loading/toggles in
  `level_entity_remap.cpp`. `bosses/` separates binding, completion, encounter
  repairs and HP guards. `remap_runtime.h/.cpp` owns shared callback state;
  preserve weak-slot guards and mutexes. Toggles affect subsequent spawns.
- Diagnostic `enemy_clear/` uses one `service_impl.h` owner, validated dispatch
  in `requests.cpp` and ordered frame progression in `update.cpp`. All private
  implementations remain excluded with diagnostics off.
- Reflection separates object/frame reads, parameter/text writes and value
  decoding; diagnostics own inspection descriptions. Ability probes retain
  per-caller follow/output limits and their shared layout catalog. GFx callers
  use `gfx_reflection_helpers.h`, keeping argument order/type tags aligned with
  the game's ABI; its internal header remains private.
- Options-menu `host.h` owns storage; components separate input, display-tree
  access, row/page presentation, anchor injection and navigation. Native focus
  callbacks and weak UI identities remain part of the host lifecycle.
- Player context preserves staging/discovery, readiness publication and the
  playable-Sora grant boundary. Controller restrictions retain enum storage and
  hook ownership. VBonus bindings keep immutable layouts separate from live
  save/UObject owners; function-address validation is process-scoped.
- Save-point resolution caches weak functions and revalidates them; serial-zero
  functions are resolved for each immediate call. Memory hooks coordinate
  instruction bytes and teardown. Autosave probe components keep caller-address
  capture at hook entrypoints and define each native owner slot once.
- Color-service components preserve discovery cursors, weak material identities,
  settings restoration and seed-bound palettes when ownership changes.

## Feature references and validation

- [Item delivery and reconciliation](features/grants/README.md)
- [Startup and save loading](../../docs/AUTOLOAD.md)
- [Map overlay](features/ui/map_overlay/README.md) and [journals](features/journal/README.md)
- [Profiling and event-query contracts](../../docs/PROFILING.md)
- [Music](../../docs/MUSIC_DEVELOPMENT.md) and [movie authoring](../../docs/MOVIE_SOURCES.md)
- [Party](features/party/README.md), [space](features/world/space/README.md) and [KH0.2](features/kh02/README.md)

Run `scripts/test.ps1` from the project root. Policy/service tests exercise
production decoders and state transitions with engine effects stubbed. Preserve
source-input/resource checks; live ABI, rendering and gameplay validation remain
separate from standalone tests and require a matching installed build.
