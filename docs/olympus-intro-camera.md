# Thebes first-entry camera

`he_03_camera_ms010` owns the six-second first-entry Matinee and its trailing
`back_camera` rotation/zoom. The existing Save Point/Moogle Shop help bypasses
only suppress the later panels; they do not prevent this camera from starting.

The object-creation observer patches the exact camera class before BeginPlay.
Its `he_03_gameflow_Evt_ms010_start` event now enters the original ubergraph at
987 (the `he_03_ms010s_REvt_ms010s_End` remote callback) instead of 1044 (the
Matinee validity check and Play call). The camera graph and gameflow remain
intact. This preserves the gameflow's cinematic cleanup, tutorial flags and
autosave, without starting the Matinee or spawning the temporary back camera.

The patch requires the exact function paths, 22-byte entry and 1151-byte graph,
linked graph/callback names, callback instruction shape and expected original
destination. Unexpected layouts are rejected without writing. It is idempotent,
reapplied on class reload and uses no timer, per-frame discovery or retained
UObject pointer.

## Validation

On local EGS, 2026-11-13:

- The native policy suite passed (169 tests). The focused test also passed
  against passively captured, linked retail bytecode.
- Built and installed the diagnostics DLL; installed hashes matched.
- Restarted through the client's Epic launch mechanism and loaded slot 0.
  The log recorded `OlympusIntroCamera result=applied` before BeginPlay.
  Passive readback confirmed entry destination 987. Thebes returned playable.
- Reloading Thebes reapplied the patch. Replays at progress 0 and the sourced
  `HE_THEBES_VISIT1` value 30 returned playable, but did not execute the camera
  entry in this existing save (its callback result remained false). These are
  load/patch checks, not proof of a complete fresh-story first-visit replay.
  The temporary progress change was restored to its original value, 30.
- During the subsequent player session, passive readback observed entry 987,
  the completion RemoteEvent returning true, and a null spawned-camera local.
  This confirms the patched entry executed without creating its back camera.

Local investigation and live evidence are in the workspace's
`build/thebes-intro-camera/` directory; no retail asset or bytecode fixture is
stored in this repository. The optional policy-test arguments accept an entry
and graph capture for local validation.
