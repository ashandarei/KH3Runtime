# KH0.2 runtime ownership

Keep KH0.2-specific native code in this feature tree. General KH3 services call
its public headers through project-relative includes; they do not own its story
or presentation state.

| Directory | Responsibility |
| --- | --- |
| `runtime` | Lazy activation, startup, entry, recovery, world context, local config |
| `progress` | Seed-bound progress snapshots, check policies, story checkpoints |
| `story` | Story coordination, source flags, loading and map sessions |
| `world` | Host maps, navigation setup, scenery, doors and portal integration |
| `cutscenes` | Matinee binding, event pawns, subtitles, visibility and movement leases |
| `movies` | Movie requests, shared-frame playback, audio, textures and surfaces |
| `ui` | HUD, minimap and area rows, mission counter, information messages, native Aqua inventory roster and camp portraits |
| `rewards` | Shared unlock flags, inventory profiles, native item notices and consumable-slot policy |
| `treasure` | Chest observation, table preparation and native journal registration |
| `challenges` | Objective definitions, presentation, counters and combat observations |
| `combat` | Enemy table schemas, transactional scaling, Demon Tide reference data and gameplay character/combo handoffs |
| `wardrobe` | Cosmetic selections, previews, accessories and material ownership |
| `transport` | Companion asset catalog, process transport, ABI adapter and file cache |
| `compatibility` | Audited source-native adapters, operand readers and aliases |
| `core` | Shared checked reflection, localization and data-table utilities |
| `diagnostics` | Runtime inspection and local test fixtures |

Register native implementation files explicitly in [sources.cmake](sources.cmake).
That file also keeps local fixture entry points behind the existing performance
diagnostics switch. Do not add feature directories to the compiler's include
search path to make bare cross-module includes work.

Use a local header name within one directory and the full
`kh3ap/features/kh02/<area>/<header>` path across directories. Generated metadata
uses `kh02/<header>` from this project's `include/` root. Shared source-game
protocols use `shared/kh02/<header>` from the project root.

The KH3 runtime remains lazy: adding a source file here does not authorize
constructing its services or polling it outside the owned KH0.2 lifecycle.

Use fast UObject validity checks and weak references across frames. Keep native
resource release explicit on the game thread. Preserve source story progression
and the established presentation teardown order when changing an owner.

`Kh02ProgressSession` owns seed/slot/revision matching, restoration and publishing.
It returns backtrack/resume decisions to the story coordinator; it does not travel
or reset a map. Its referenced flags, wardrobe and challenge state must outlive it.

`Kh02OpeningScene` owns the DW009 stream, script and Matinee weak references and
their prepare/start/skip/unload lifecycle. Its context is borrowed per call. Aqua
and the intro presentation remain shared with the surrounding story; unloading
hides Aqua when the current gameplay world still owns her, otherwise destroys her.

`Kh02ChallengeHooks` owns executable validation, native detours and observer
registrations. The tracker owns challenge attribution and scoring. Damage dispatch
keeps the original game call between the tracker's before/after observations so
defeat notifications remain authoritative. Stop registrations before destroying
the tracker state referenced by callbacks.

The remaining runtime boundaries are organized as follows:

| Owner/helper | Boundary |
| --- | --- |
| `Kh02StoryEventRouter` | One process listener routes to the active lazy story owner; detaching an owner leaves no captured session pointer |
| `Kh02CastleGameplaySession` | Castle stream bindings, source setup, retry/backtrack state and gameplay readiness |
| `Kh02MirrorGameplaySession` | Hub/room initialization, source-script binding, forest updates and mirror presentation state |
| `Kh02MirrorTravel` | Validate and stage travel during source parsing, commit after parsing, dispatch after the source stack returns |
| `Kh02MoviePlaybackState` | Typed phases, generation, resume, stop and completion state |
| `Kh02MovieSourceContinuations` | Audited source waits and ready/finish callbacks, with movie generation checked after reentrant calls |
| `Kh02RemotePipeTransport` | Connection handles, serialized RPC, reconnects, server validation and transport metrics |
| `Kh02StoryFlagBindings` | Native flag thunks, audited operands, forwarding and original-function restoration |
| `Kh02LoadCompletion` | Copied native delegate lifetime, callback owner validation and unavailable-load dispatch |
| `Kh02WardrobeEditTransaction` | Nested color-preview rollback points, commit/cancel and bounded history |
| `Kh02RemoteReadService` | Cache selection, settings and fallback; loaders run outside the cache lock |
| Provider preparation | Readiness worker, cancellation, bound/imported identities and irreversible native binding status |
| Companion process | Verified source launch, private desktop, job/process/control ownership and published health handles |
| `Kh02CombatEventJournal` | Bounded hit/death history, authoritative defeated identities and attribution windows |
| Progress codec | Snapshot JSON parsing, serialization and validation; checkpoint entry policies stay separate |
| `Kh02ChallengeMovieLease` | Movie preparation, acquired asset roots and native-menu visibility restoration |
| `Kh02ChallengeInputRouter` | Physical key ownership, key-to-action decoding and existing repeat-navigation policy |
| `Kh02WardrobeFocus` | Deferred focus, anchor resolution and cursor-position caching |
| `Kh02WardrobeMovieView` | Scaleform presentation arguments, labels, palettes and history rendering |
| `Kh02StoryDiagnostics` | Diagnostics-only snapshots using synchronous borrowed domain views |

`Kh02GameplayBinding` contains only shared weak world/pawn/script bindings. Session
`Services` arguments are borrowed for one synchronous call. Keep source event order
in the story coordinator when an arrival crosses multiple domain owners.

`Kh02SessionRetirement` borrows domain owners for one synchronous cleanup. Its
source-replacement, failure and world-departure plans retain their distinct release
order, wardrobe restoration and exception policies. World observation/source
invalidation can reenter the replacement plan; iteration and error collection are
local to each call. Progress and native aliases survive session retirement.

`Kh02MapSessionState` owns the once-per-arrival menu-music release gate. Re-arming
an active arrival retains that work; arming after completion, failure or cancellation
starts a new gate. JSON proof records the result and never determines whether to
release the menu slot.

`Kh02EncounterSceneBinding` owns encounter preparation, event-Aqua binding/showing
and opening/post-boss continuation selection. It borrows weak world/pawn bindings
and existing owners synchronously, rereading them after native callbacks. The
native thunk still runs preparation, binding and its original function in order.

The remote pipe is registered for destruction before the asynchronous read runtime,
and the companion process before provider preparation, so workers finish before
their underlying resources are destroyed. An imported provider identity remains
bound for the process lifetime; refactoring must not introduce a rebind fallback.

Additional boundaries keep the remaining native details out of their coordinators:

| Owner/helper | Boundary |
| --- | --- |
| `Kh02StoryNativeOverrides` | Four validated native function replacements, original thunks, restoration and guarded encounter listener |
| `kh02_decode_damage_event` | Copy and classify audited native damage metadata before the original damage call can retire it |
| `Kh02MovieDecoderLease` | Control ticket, shared mapping and source-process handle; audio/UI remain separate |
| `Kh02NativeFileAdapter` | Audited executable/ABI profile, named dispatch slots, installation and original forwarding |
| `Kh02PostBossHandoff` | Weak Matinee binding, observed playback, validated exit request and deferred Castle-to-hub dispatch |
| `Kh02ChallengePlayerStateObserver` | Accepted player-state identity, pending rail resolution and ice-slide sampling |
| Checkpoint rules | Canonical typed entry rules; JSON adapters preserve original keys and unrelated values |
| `Kh02MovieFramePresenter` | Existing frame queue selection, upload/show timing and final-frame hold |
| `Kh02MusicSlots` | Native SEAD slot operations; story music still chooses cues and entry behavior |
| Diagnostic action handlers | Typed Castle, movie, mirror and opening commands, with explicit borrowed services and snapshots after mutations |

The story native lease captures replacement identities before changing any
function, so partial installation can still be restored. Preserve alias phase
ordering in the composition root. Post-boss playback observation is committed
before reading its position, and retry clears observations independently of a
pending source exit.

Movie teardown remains explicit: cancel the request, stop audio, release the shared
mapping, close the source-process handle, then stop the surface. Native file
dispatch is restored before asynchronous reads drain. Diagnostic action handlers
and their composition adapter remain excluded from player builds.

Arrival, preview, asset and rule responsibilities have these additional boundaries:

| Owner/helper | Boundary |
| --- | --- |
| `Kh02ArrivalPresentation` | PRE_FG cover, native loading UI and balanced controller input claims; phases/readiness stay in MapSession |
| `Kh02SourceScriptRegistry` | Weak world/script identities and generation invalidation before presentation retirement |
| `Kh02PreviewRenderLease` | Preview capture actor, lights, widget/texture roots and camp rendering setup/cleanup |
| `Kh02PreviewSchedule` | Independent material-load timeout and capture cadence/reset boundaries |
| `Kh02WardrobeCompanionMovies` | Lazy background/cursor movies, acquired asset roots, visibility and cleanup |
| `Kh02WardrobeUnitView` | Synchronously borrowed, checked attachment-unit schema/storage; attachment and pattern effects stay in dressing room |
| `Kh02EnemyStatAssets` | Requested assets, acquired roots and loaded/prepared/base/Tide table references |
| `Kh02BattleLevelObserver` | Guarded native battle-level events; scaling, row mutation and restoration stay in EnemyStats |
| `Kh02AbilityCapacity` | Aqua's 200 base AP, prepared before pawn creation and restored before entry assets are released |
| `Kh02CombatSwap` | Lazy R2 gameplay swaps with camera/locomotion continuity and one-shot native combo-index handoffs; camp retains character preference/input ownership |
| Challenge progress/combat rules | Named goals, shared mutation commit rules, species/hit matching and defeat attribution |
| Challenge encounter-site policy | Audited source operations, caller sites and no-damage attempt boundaries |

Keep source identity captured across a retirement callback: invalidation clears the
registry and advances generation before that callback runs, then the new script is
recorded. Arrival cancellation clears the cover and independently attempts owned
input release before resetting its weak bindings. Preview resource rebuilding must
not extend a pending material deadline. Enemy teardown stops its observer, restores
the changed rows/combat data, then releases only asset roots acquired by its lease.

See the [project guide](../../../../README.md) for native builds and tests and
the [data contract](../../../../docs/DATA_CONTRACT.md) for local metadata ownership.
Source-game setup and the other integration projects are maintained separately.
