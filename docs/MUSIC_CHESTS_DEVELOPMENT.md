# Music chest development reference

Player music-chest instructions are documented with the separately installed KH3 client.

## Chest conversion and song handoff

Converted chests retain their actor, mesh, animation, treasure ID, position,
logic and reward. The runtime changes the material to the prototype's blue/gold
remapping of the original diffuse texture and changes Open to Examine until the
song is cleared. A clear enables Open; the original chest-opening path sends
the existing AP check. Failure/abort does not grant that check. Verified clears
persist in seed-scoped client state and reapply on revisit/reconnect.

Interaction pauses KH3 only if it was not already paused. It resumes after
focus leaves and returns to KH3, including a return without a clear. If focus
never leaves because launch fails, the owned pause expires after 30 seconds.
The same pause handling is used by the additional-chest diagnostic prototype.

The owned pause also mutes **all KH3 audio** (music, effects, and voices) through
the current process's Windows render sessions. Returning to KH3, the failed-launch
timeout, or retirement of the paused world restores each session's prior mute
state. Volume levels and saved in-game audio settings are untouched; a session
that was already muted stays muted. The audio worker discovers new sessions
once per second during the handoff, retries failed restores, and restores on
service shutdown. Other processes, including MoM, are excluded.

`test_kh3_mom_audio` covers state preservation and restore retries;
`test_kh3_mom_audio_windows` uses silent WASAPI streams to check actual mute
readback, unchanged volume, new sessions, and shutdown restoration. Full chest
handoff and Wine/Proton audio behavior still require in-game validation.

## Runtime assets

The callback, note textures, chest materials, and dome/chest prototype assets
are bundled in Garden of Assemblage. Their original `/Game/Mods/...` paths
remain unchanged. Stock chest meshes and animations are still referenced from
the game and are not repackaged.

These assets belong to the separately maintained Garden of Assemblage project.
Install its compatible pak separately when testing this feature. This DLL
project contains no Unreal project, cooking scripts, or GoA pak builder.

The music chest runtime is compiled into normal diagnostics-off probe builds.
Agent spawn/probe commands remain behind the existing diagnostics flag.

Native conversion and pause/focus behavior still require a fresh in-game test
of this production integration; the earlier prototype verification does not
establish behavior across every vanilla chest material.
