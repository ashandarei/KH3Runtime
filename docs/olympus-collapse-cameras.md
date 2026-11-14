# Thebes collapse cameras

`he_03_camera_ms024s` owns both the distant building collapse and the later
downhill collapse. The first uses a Matinee director cut and an `OTS` camera;
the second selects `exp01_cam` or `EXP02_cam` based on a line trace.

The construction observer patches the exact retail 11501-byte graph before
BeginPlay. Six fixed-size jumps skip the three camera setup branches and their
three cleanup/reset calls. Seven additional jumps remove the cinematic-mode
enter/exit calls and the downhill sequence's wait for cinematic pawn state 67
and forced `rt001` animation. That wait would otherwise retry indefinitely
after removing the movement lock. The original scenery/shake sequence starts
directly, while the preceding on-ground checks remain intact.

The exact gameflow completion event now enters its original field-voice
callback at 19103 instead of its motion-reset wrapper at 463011. This avoids
forcing Sora into `IdleN` and waiting another 0.6 seconds after the first
collapse. The original Matinee Play calls, sound, shake, companion animation,
rubble destruction, progression flags, scenery delays and completion
RemoteEvent remain intact.

The existing ProcessEvent callback facility disables only the two referenced
Matinee director tracks on this exact level-script class, at BeginPlay and the
first-collapse start event. It also keeps the first Matinee's Sora movement and
full-body-animation tracks disabled (already disabled in the inspected retail
asset). The second Matinee's group 0 belongs to scenery and is preserved. This
runs after actor references are serialized and before playback; event tracks
on the same director group remain enabled.
Unrelated events get only FName comparisons. There are no timers, per-frame
object searches or retained UObject pointers. Track lookup is class-qualified
and relative to each current Matinee's path, avoiding stale lookup caches.

The character-swap cinematic handler also checks the disabled tracks for these
two exact scenery timelines. The first still declares a Sora mesh role, even
though all its tracks are disabled. Previously the role alone triggered a
native Sora handoff when playing Kairi. A role in these timelines is now ignored
only when it has a nonempty, valid track list and every track is disabled.
Other Sora roles, enabled tracks, unknown layouts and unrelated movies retain
the ordinary binding/handoff policy. This check runs only at the existing
Matinee event and retains no object pointers.

Character adoption is idempotent for the already owned pawn in the same world
with the same selected character. Scenery Matinee playback also queried this
path and previously queued full character preparation, even when no cinematic
handoff was needed. That reapplied the captured ground movement settings during
glide, reducing Kairi's live speed limit from 1500 to 680 until glide restarted.
New pawns, changed worlds and changed character identities still require the
normal adoption and preparation path.

The bytecode patch validates graph identity/size, linked native functions,
camera FNames, instruction shapes and jump destinations before any write.
Unknown or partially modified layouts are rejected; repeated application is
idempotent. Policy tests check atomic rejection and that no bytes outside the
six five-byte jumps change. A local linked-bytecode capture can be supplied as
the optional argument to `test_kh3_olympus_collapse_camera`. The movement test
accepts three local captures: camera graph, completion event and gameflow graph.

## Local EGS validation, 2026-11-13

- Diagnostics build and all 171 native policy tests passed. Installed DLL
  hashes matched. Startup used the client's Epic launch path and slot 0.
- Live logs reported the graph patch before BeginPlay and director disabling.
  Passive readback verified all six jumps and both `bDisableTrack` values.
  The focused policy test also passed against the captured linked graph.
- From story progress 120, entered the inspected first-collapse trigger.
  Its Matinee reached 5.6333337 seconds, the original completion RemoteEvent
  returned true, and the rubble-destruction event ran (frame entry 6384).
- Entered the downhill trigger. Its Matinee reached 11.000001 seconds and
  the final rubble-destruction event ran (frame entry 6575).
- All three spawned-camera locals remained null through both events. Both
  events returned stable playable Sora, with explicit inactive special battle
  state. Temporary protection was restored afterward.
- Repeated both events after removing character locks, with six alternating
  1.2-second normal movement inputs per event. Every hold ran its full duration.
  Passive samples observed actual horizontal movement while the first timeline
  advanced from 0.046 to 5.575 seconds and the second from 0 to 10.962 seconds.
  Both completed with no zoom camera spawned and no active battle. The policy
  test also passed against linked retail movement/completion bytecode.
- At the user's request, reloaded the pre-event autosave for manual testing.
  Verified story progress 120, the collapse boolean false, neither event run,
  playable Sora at the original checkpoint, no battle and protection off.
- Repeated both collapse triggers as Kairi after fixing the inactive Sora role.
  All 171 policy tests passed, followed by a focused character-policy rerun.
  Both timelines reached their normal ends, with rubble/completion events intact
  and all three zoom-camera locals null. Across 222 first-event and 219 downhill
  samples, the local controller and possessed Kairi pawn stayed unchanged.
  Six 1.2-second movement holds per event produced actual movement during the
  scenery animation. Both ended with Kairi controlled and no special battle.
  Evidence is in the workspace's `build/thebes-collapse-kairi/` directory.
- Restored slot 0 and selected Kairi for the user's manual retest. Readback
  confirmed story progress 120, collapse flag false, both timelines at zero,
  Kairi control ready at the original checkpoint, no battle and protection off.
- Reproduced the reported glide slowdown with a native held-jump approach to
  the downhill trigger: Kairi remained in `TresPlayerState_Glide`, while
  `MaxWalkSpeed` dropped from 1500 to 680 when the Matinee started.
  After the adoption fix, both first and downhill trigger crossings kept the
  live limit at 1500. All 32 first-collapse and 39 downhill samples recorded
  during glide and timeline playback also retained horizontal velocity above
  1350 units/second, without a second glide press. Both timelines completed,
  their camera locals stayed null, and protection was restored. All 171 policy
  tests passed. Evidence: `build/thebes-collapse-glide/` in the workspace.

These were native trigger tests using inspected same-map positions, not a
continuous manual walk or a visual recording. Local evidence lives under
`build/thebes-camera-followups/` in the workspace; no game assets or retail
bytecode are stored in the repository.
