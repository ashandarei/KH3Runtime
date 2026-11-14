# Native automatic cutscene skipping

`auto_skip_cutscenes` now requests the game's normal cutscene Skip operation
through the UI manager's native coordinator. It starts the game's fade and
deferred skip callback without opening the pause menu or injecting input. The
game's scene/gameflow callbacks own placement, story changes and map travel.
There is no destination-coordinate table and no guessed Blueprint `End_skip`
dispatch.

## Lifecycle and executable guards

The existing `TresSetPauseMenuType` post hook captures the cinematic actor and
current world as weak references. It reads the authoritative game-instance
pause type rather than interpreting the Blueprint VM's argument memory. Native
skippable type 1 arms automatic work for a valid current-world owner, including
encounter camera scripts under `/Game/Levels/`. Asset folder names do not decide
eligibility. Normal gameplay and native unskippable pause types do not arm skips.

Dispatch occurs on a later game-thread update, outside the scene's Blueprint
callback. Each scene occurrence gets a revision and at most one native request.
A chained scene creates a new transaction, including when the pawn is unchanged.
Reentrant callbacks cannot overwrite the new transaction with an older result.

An already-open pause menu or pending native fade defers the unissued request.
Readiness checks run at most every 50 ms while that transaction is unfinished,
with a 15-second deadline. Idle updates do not discover scene/UI objects. An
issued request is never repeated after a timeout. Actor/world loss cancels an
unissued request; a change after dispatch is recorded as a transition, not as
proof of playable arrival. The existing bounded Monstropolis fade handoff repair
remains available for its two specifically identified scenes.

Native profiles validate executable identity, code signatures, the UI manager
and pause-panel vtables, and manager ownership. The coordinator RVAs are
`0x18fb510` on the audited EGS executable and `0x15a82a0` on the audited Steam
executable. Unsupported layouts fail closed. Static analysis traced both
coordinators through their fade completion delegate to the game-instance skip
operation. Steam/Proton execution has not been validated by this change.

## Live EGS validation, 2026-11-13

Used the existing `87359377803302292363 / StunTest` seed with automatic skipping
enabled. No helper `--skip-cutscenes` option or manual pause-menu action was used.
Diagnostic DLL SHA-256:
`0e122940707e07531ea649a6aefc0b7d27f342f892e1b5483e7aeb5de6ca1b33`.

| Route | Automatically skipped scenes | Observed ending |
| --- | --- | --- |
| Olympus summit at `gameflow_HE=2200`, through native Titan defeats | `he117`, `he118_subtitle`, `he119_subtitle` | Playable GoA; `gameflow_HE=9999`; inactive battle |
| Monstropolis first entry at `gameflow_MI=0` | `mi351`, `mi352`, then `mi353` after the opening fight | Playable `mi_01`; `gameflow_MI=1030`; inactive battle |
| Toy Box first entry at `gameflow_TS=0` | Back-to-back `ts251` and `ts252`, then `ts253` after the opening fight | Playable Andy's Room; `gameflow_TS=1020`; inactive battle |

Toy Box's first request was recorded at 17:23:50 UTC and its transition at
17:23:52. The second request was at 17:23:57, its transition at 17:23:58, and
stable combat control at 17:24:01. Both requests were separate scene revisions.
After the encounter and `ts253`, final inspection showed zero enemies, HP 90,
enabled control, no blocking UI and test protection disabled. Sora stood near
`(4891.79, -1410.16, 2931.65)`. The session was left there.

The Olympus run reached and skipped the later ending chain, but did not record
native requests for `he115` or `he116`; do not claim individual coverage of those
two scenes or a reproduction of the original Steam Deck crash. The old guessed
`End_skip` route implicated in that crash is removed. Full-game scene coverage,
Steam/Proton behavior, and live option-off/menu-contention cases remain untested.

The bounded enemy-clear helper reported a context change during the Olympus
phase handoff and rejected further waves once Monstropolis/Toy Box entered a
cutscene. Those are not claimed as successful clear-helper runs. Subsequent
native battle/scene/readiness evidence established each route's completion.

Diagnostic and player builds passed. The player build records diagnostics and
automation handlers disabled. All 20 Python runtime checks and 168 native policy
tests passed, including duplicate notification, scene replay, reentrant scene
change, weak-context cancellation, busy deferral and timeout cases. The diagnostic
DLL remains installed; the UE4SS loader and APWorld package were not replaced.

Local evidence is retained under the workspace's `.local/cutscene-native/`,
including `events.jsonl`, route/arrival/flag snapshots, protection restoration,
screenshots, build records and `summary.json`. These are test artifacts, not
runtime build inputs.

## Repeating the Toy Box case

Use the maintained KH3Tools map helper with `/Game/Levels/ts/ts_01/ts_01`,
`ts_01_Lv_Start_01`, and checkpointed `gameflow_TS=0`. Require automatic skipping
enabled in the installed seed. Omit the helper's manual `--skip-cutscenes` flag.
Allow combat-control readiness and enable bounded protection immediately on
arrival. Expect separate request/transition records for `ts251` and `ts252`,
then verify the actual pawn, loading, battle and input state. Complete the native
opening waves under protection to exercise `ts253`; require inactive battle and
full gameplay readiness before ending the test. Restore test protection.

## Olympus encounter camera regression

The first implementation missed the first Olympus Heartless-spawn scene.
Live inspection of the user's paused scene showed native pause type 1 and
`he_02_camera_ms001_C_1` under `/Game/Levels/he/he_02/umap/`. The `/Game/Cutscene/`
folder filter excluded it even though the native pause menu offered Skip Scene.
The filter was removed; the authoritative pause type and current-world lifetime
guards now decide eligibility. Normal type 0 and unskippable type 2 remain excluded.

The corrected diagnostic DLL hash is
`e608d8ff42ddf3d1b51f0a8d32467bd8d2e0b91592a1f4b1712b3d6accde1faa`.
Both corrected diagnostic and player builds passed. On EGS process 43132,
replayed Olympus from `gameflow_HE=0`, `he_50_Lv_Start_01`. Both `he101` and
`he102` still skipped. The first encounter's `lv_EventTriggerBox4` was verified
passively: root center `(4321.4526,15154.1104,-5010)`, half extents `(400,2400,1200)`,
yaw -20 degrees. Entering at `(4321.45,15154.11,-6053.98)` triggered the actual
scene. A direct jump to enemy spawn positions had landed past this trigger and
is not counted as a scene test.

At 17:36:25 UTC, `he_02_camera_ms001` recorded its native request; at 17:36:26
it recorded the transition. Stable combat control returned at 17:36:28. The
subsequent snapshot showed 11 enemies, active battle and Sora positioned by the
game at `(3390,15790,-6220.85)`. No manual skip helper or pause-menu action was
used. The encounter was completed under protection; the final snapshot showed
inactive battle and zero enemies, and protection was restored afterward.
The final control-readiness wait passed with input enabled and loading/UI closed.
The stricter AP `map_ready` wait timed out despite that playable state; this
follow-up establishes scene skip and combat handoff, not AP grant-readiness repair.

Evidence uses the `olympus-missed-*`, `olympus-opening-*`, `olympus-ms001-*` and
`build-camera-*` files under `.local/cutscene-native/`. The corrected diagnostic
DLL is installed. This extends live coverage to the reported encounter camera;
it does not establish all-camera or Steam/Proton coverage.

## Alternate character restoration after level cameras

The Olympus statue break (`he_03_camera_ms027`) exposed a second folder filter
in character restoration. The existing cinematic binding guard could return
Kairi to Sora, but the native type-0 completion notification discarded sources
outside `/Game/Cutscene/`, retaining the temporary cinematic hold indefinitely.
Character start/finish notifications now accept valid current-world sources
regardless of their asset directory. The authored-role binding checks and
temporary Sora fallback are unchanged.

Diagnostic and player builds passed, as did the focused character travel and
cutscene transaction tests. Diagnostic DLL
`1c5cae611a772d4e031a3930e63ee218cb3a535ff4c6eb7dfefac31f76795517`
was installed and loaded on EGS process 60676. Exact statue replay remains
unverified: lowering the main Olympus flag and entering the he108 trigger did
not replay the statue's separate gimmick event. The attempted boolean setter
timed out on its safe-Sora gate without changing the already-false Achilles
flag. Do not count these attempts as successful scene coverage.

The main Olympus flag was restored from the temporary 120 to its recorded 420,
and the final native snapshots verified Kairi (16), enabled gameplay control,
inactive battle and test protection disabled. Evidence is retained in
`.local/cutscene-native/statue-final.json`, `statue-final-combat.json` and the
other `statue-*` artifacts. The remaining live check is an actual statue break
with an alternate character, confirming temporary Sora followed by automatic
restoration of that character.

## Keeping alternates in presentation-only map cameras

The subsequent character-binding change supports multiple Sora roles in map
Matinees when every enabled player track is either `TresInterpTrackVisibility`
or `TresInterpTrackWeaponControl`. Olympus's statue camera has both `p_ex011`
(Olympus Sora) and `p_ex001` (normal Sora) roles; both initially have null actor
slots. Both now target the controlled alternate through the existing native
actor-tag resolver and reflected `GroupActorInfos` slots.

This is deliberately a track-based extension for `/Game/Levels/` scenes.
Movement, animation, unknown enabled tracks, nested tracks, custom target tags,
missing slots, non-player actors and distinct existing actor identities do not
qualify for shared binding. Existing single-role handling and the Caribbean,
Re Mind door and Ascension ownership exclusions remain in place. The camera,
statue/scenery tracks and native skip/gameflow completion are unchanged.

All targets are checked before writing bindings. Originals are retained per
scene/group using weak references; the shared pawn tag is removed only after
its last live binding is released. No idle scans or new per-frame discovery
were added.

The diagnostic-only `olympus_statue_scene` command invokes the loaded camera's
authored `he_03_gameflow_REvt_Achilles_Start` entry after verifying current-world
ownership, playable control, inactive battle and no enemies. It enumerates from
the permanent native LevelScriptActor class to avoid retaining a streamed
Blueprint class across a camera reload. Dispatch alone is not a passing test.
The maintained KH3Tools `cutscene_character_test.py` verifies the native skip
transition, both role bindings, absence of a Sora handoff, unchanged pawn
identity, returned control and visible mesh with the temporary tag removed.

The Kairi replay passed on EGS process 46320: `ms027` bound both roles to
`p_ex006_Pawn_C_0`, native skip completed, and the same Kairi pawn remained
visible and playable. The trace contains no Sora-ready event during that
scene. Evidence: `.local/cutscene-native/keep-pc-kairi-complete.json`.
The player and diagnostic builds, 20 runtime Python checks, all 172 native
policy tests and 42 enemy-bridge Python checks passed. Full-duration playback,
other map cameras and Steam/Proton are not established by this replay.

The final diagnostic build
`c6cbb508e42cca817da0c8b88c748b1ff18a831a4b09faa56c02299622c5c090`
also passed the complete Kairi replay on process 38152, including visibility,
tag cleanup and unchanged possession (`keep-pc-final-build-verified.json`).
Roxas selection was rejected by this seed's character ownership. A manual
character token acknowledgment does not populate the authoritative AP receipt
catalog; it is not unlock or scene coverage. No Roxas success is claimed.

## Alternate characters during map arrival

The player-start rebind previously waited for normal control readiness, which
includes the loading screen being closed. That exposed the newly spawned Sora
before restoring the selected alternate. Pending travel work can now use the
existing native character factory while the loading screen still covers a new
ground world. It requires a current-world Sora pawn possessed by the actual
local player, retained character intent and ownership, and the existing health,
story, cinematic, space and Ascension guards. It adds no idle discovery or timer.

This is shared by ordinary `/Game/Levels/` arrivals and GoA, not an Olympus-only
override. Caribbean, Gummi space and Re Mind keep their normal readiness path;
same-world cinematic returns also retain their previous timing. Native story
travel that explicitly clears character intent remains authoritative.

EGS diagnostic build
`cc93ae34d05c4b65443614b3e31d927c4ac2920871aebf1f0a3983b0b461c1df`
restored Kairi under loading cover during physical Olympus door crossings and
native map arrivals in GoA, Toy Box and Thebes. Destination checks verified
Kairi possession, enabled control, inactive battle, and destination camera and
party ownership. The native jump cases test arrival behavior, not physical
doors in those worlds. Evidence is under `.local/character-travel/general-*`.
Both build modes and all 172 policy tests passed. This establishes native
loading-cover timing; it is not a frame-by-frame visual capture or exhaustive
coverage of every map, character, cutscene or Steam/Proton.
