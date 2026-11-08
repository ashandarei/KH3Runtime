# KH0.2 runtime ownership, loading and recovery

This reference describes the maintained native integration. Asset discovery,
compatibility edits and cache limits are documented in
[kh02-asset-loading.md](kh02-asset-loading.md). Source owners are under
`KH3APProbeCpp/kh3ap/features/kh02/`; authoritative profiles and area metadata
are under `authoring/kh02/`.

## Runtime ownership

`Kh02EntryService` owns the resident bootstrap and portal. It constructs the
story runtime only when preparation starts. `Kh02Lifecycle` separates dormant,
preparing, active and leaving states. `kh02_world_context` classifies the current
viewport world using weak identities.

Dormant worlds do not discover a chapter player or poll story, HUD, challenge,
movie or provider state. Portal requests and pending preparation wake the
relevant owners. Departure releases entry roots and owned input/presentation
state once, then suspends gameplay work. Native import aliases remain registered
because the engine may retain their thunks after departure.

Scene operations are typed prepare, start, skip and unload requests. Diagnostic
string dispatch wraps those operations only in diagnostics-enabled builds.
Chapter music, movie playback and field presentation have separate owners;
an optional audio failure cannot prevent the opening's gameplay handoff.

## Chapter state and source contracts

Source levels, scripts, events, Matinees and actor bindings retain progression
authority. Compatibility adapters translate verified source types and native
calls; loading an asset or dispatching an event does not fabricate completion.
Readiness requires the actual destination, settled required streams, the current
possessed player and completed loading/presentation handoff.

Chapter flags, challenge state, cosmetics and chest-journal data are isolated
from ordinary KH3 world state. Reconciliation preserves unrelated fields and
bits. Area metadata and resume policies select supported entry/checkpoint tags;
an interrupted opening is not treated as completed field entry merely because
a visit bit exists. Tower's completed checkpoint is 410.

`Kh02ProgressService` handles client chapter reads, writes and resets without
constructing the story or starting the provider. A pending command retains its
seed, slot, epoch and revision until authoritative readback. Writes require
playable Garden Sora. Reset preserves unrelated native journal bits and
invalidates an existing story owner through a synchronous notification.

Source DataTables and imported rows require their verified native schemas.
Treasure setup resolves the live viewport world through `kh02_current_world()`;
a bootstrap context may be a `UWorld` and must not be cast to `AActor`.
Retained UObjects use weak handles and the runtime's fast validity checks.

The World Within boss-rush mirror unlocks when `gameflow_REBELLIS_CLEAR` is set,
without requiring Zodiac relics. Its BeginPlay/visibility notifications retain
the authored actor weakly; after entry settles, the runtime requests its original
`StartGimmick` activation once with `PlayAnimation=false`, so revisits show it
immediately without the `vg_dw03_cha_app_par30` appearance burst/flash.
Objective 43 still requires all twelve Zodiac chests and is not awarded by
showing the mirror. EGS live validation
confirmed an absent mirror before clear and a visible, collidable mirror after
clear with only five Zodiac chests opened.

## Native save loading

With the chapter provider enabled, `Kh02Bootstrap` installs a native
slot-load interceptor through the supported loading profile. Ordinary
destinations bypass chapter preparation, including after a failed chapter
session. Imported destinations re-arm preparation on every load; registered
aliases alone do not prove that entry roots remain available.

Destination classification reads the already-loaded native save catalog.
It does not inspect vanilla save files. Missing or malformed destination
metadata takes the native unavailable-load branch.

The gate retains the chosen slot, native options, a native-owned copy of the
completion delegate and weak identities for its UI owner, manager and viewport
world. The game thread releases that request only after the provider is
authenticated and bound, client progress is ready and chapter assets and
compatibility setup are complete. The original native operation owns actual
deserialization and its completion callbacks.

Another selection cannot overwrite the retained request. Its maximum retention
is 180 seconds. Owner/world retirement discards it; a still-current owner gets
one native unavailable-load completion on preparation failure or timeout.
The gate never synthesizes success, chooses another slot or changes story flags.

## Loader and callback validation

Supported profiles pin the executable identity and native loader signatures.
Installation verifies the full loader prefix, the actual detour and stolen
instructions, the trampoline allocation and its return to untouched code.
An unsupported layout or failed interception stops initialization rather than
allowing imported save deserialization without its preparation gate.

Delegate copies and destruction use the game's native allocation and virtual
operations. Retained callbacks must prove the expected UI ancestry and manager
ownership. Only the inspected Continue functor uses its known raw capture;
other delegates require a valid bound UObject. Weak owner checks run again
before replay or failure completion.

Death-menu Continue additionally validates its GameOver delegate, owning Pause
UI and manager links. Asset preparation uses the verified current World as its
Kismet context. Boss Retry keeps its separate native delegate path.

`loading_runtime::native_load_gate_failure()` publishes a monotonic failure
sequence, request generation and reason. Diagnostic observers accept only newer
failures. A successful title state is not evidence of cancellation.

## Provider failure and recovery

`kh02_asset_companion` binds readiness to the exact owner/source process,
catalog, generation and nonce. A process loss cannot reuse an earlier ready bit.
The game thread reads an immutable health snapshot; it never waits for source
verification or launch. Once the remote adapter is bound, that identity is
permanent for the KH3 process. A failed bind or later source loss requires a
KH3 restart; a still-ready provider may be reused before that failure boundary.

Typed map, movie, cutscene and compatibility failures retire callbacks and owned
presentation/input without completing a source graph or changing progress.
`Kh02RecoveryService` requests a normal Garden return through existing travel
safety gates, allowing up to 15 seconds. Once Garden is playable and unpaused,
it attempts one six-second native information banner explaining the restart.
Arrival readiness has a 30-second limit. Optional audio/cosmetic warnings remain
local to their owners. Recovery stops updating once its work completes or times
out, and a failed chapter session stays retired until KH3 restarts.

Recovery inspection retains the original reason and source identity while its
phase advances from failed to exiting to returned. It exposes pending return,
notification attempt/display and retired movie state. These checks operate at
an observable game-thread boundary; they cannot resume a partially deserialized
package, recover an arbitrary native fatal error or unhang the game thread.

## Challenges and reward ownership

The challenge tracker consumes authoritative source events, private chapter
flags and native defeat updates. Weak enemy identities prevent duplicate credit.
Elemental/aerial credit requires the matching player-owned typed hit and a
confirmed defeat. Generic damage cannot establish spell or aerial completion.
No-damage goals require the original encounter window and completion path.
Unsupported conditions remain Pending; profile/policy coverage alone does not
establish live encounter coverage.

AP ownership stays with the verified seed/player and its receipt ledger.
The explicit ready-Aqua reward adapter preserves Sora's ability/bonus ownership
and Aqua's separate records while reconciling shared controller capabilities.
It waits through loading, menus, cinematic control and unhealthy source sessions.
Other reward methods retain their existing context gates. Source chapter events
must not create an additional AP receipt or infer a successful grant from a UI
popup alone.

## Diagnostics

`story_runtime_inspect`, `story_runtime_provider_loss` and
`story_runtime_recover` are diagnostics-only handlers. The loss action requires
an active movie and the exact runtime-owned source; it cannot stop an unrelated
installation. Recovery can request one guarded Garden return when needed.
Tests compare chapter flags, progress, AP checks and movie retirement rather
than treating a dispatched command as success.

The APWorld checkout owns the live runner in `tools/kh02/regression.py` and its
operator instructions in `tools/kh02/REGRESSION.md`. They are optional external
development tools, not native build or release dependencies.
