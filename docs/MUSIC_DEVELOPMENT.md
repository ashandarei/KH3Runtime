# Music development reference

Player music settings are documented with the separately installed KH3 client.

## Custom audio formats and configuration

WAV tracks use PCM or 32-bit IEEE float, including those WAVEFORMATEXTENSIBLE
subtypes; OGG tracks use Vorbis. Tracks support up to eight channels at 192 kHz.
Each encoded file and decoded sample buffer is limited to 256 MiB; oversized
or malformed tracks are rejected.
Vorbis setup and scratch memory share a separate 32 MiB limit.
WAV format extensions must fit their chunk. The optional manifest is limited to
4 MiB, and loop times are clamped to the decoded track before playback.

`TARGETS.txt` in that directory lists supported track, world, level and form
targets. Examples include `BGM_he_battle`, `GoA_01`, `Olympus`,
`he_battles`, `he_exploration` and `Form_UltimaWeapon`. Matching uses the most
specific applicable target: level, track, world scope, then world.

A file's name supplies its target. Optional `custom_music.json` entries add
explicit targets, loop points and per-track gain:

```json
{
  "tracks": [
    {
      "file": "garden.ogg",
      "replaces": ["GoA_01"],
      "loop_start": 12.5,
      "loop_end": 184.0,
      "volume": 0.8
    },
    {"replaces": ["BGM_he_battle"], "vanilla": true}
  ]
}
```

Explicit replacements apply in every shuffle mode. A file replacement holds
form themes back; a file-free `vanilla` target stays out of the shuffle but
still permits form themes. A `Form_*` file replaces that form's theme even
when Form Themes is off; a `Form_*` vanilla entry disables it. Include Custom
Music controls the Chaos pool, not explicit replacements. Files remain local
to the player.

Playback uses the game's volume settings and the track gain. Suspended native
slots retain their ownership; stale decode results must not replace a newer
selection. [Now Playing](../authoring/kh3/now_playing/APNowPlaying.as) displays the native track until
custom playback starts; pending or failed decodes do not become custom titles.

## Source ownership

The native files below are under
[`features/music`](../KH3APProbeCpp/kh3ap/features/music/).

| Owner | Responsibility |
| --- | --- |
| `shuffle/bgm_shuffle_table.cpp` | World/group catalogue, deterministic assignments and exclusions. |
| `audio/custom_music_library.h/.cpp` | Target precedence, manifest parsing, library discovery and `TARGETS.txt`. |
| `audio/custom_music_player.cpp` | Local decoding, playback and completion/lifetime handling. |
| `shuffle/form_theme.h/.cpp`, `shuffle/bgm_shuffle_forms.cpp` | Form detection, theme ownership and restoration. |
| `shuffle/bgm_shuffle_service.cpp`, `shuffle/bgm_shuffle_custom.cpp` | Event-driven reconciliation and custom/native slot routing. |
| `events/sead_bgm.h/.cpp` | Reflected SEAD access and checked native layouts. |
| `events/sead_music_profiles.h`, `events/sead_music_event_sites.inl`, `events/sead_music_events.cpp` | Executable profiles, guarded hook sites and lifecycle notifications. |

KH3's manager has nine named native slots. Shipping reflected layouts take
precedence over assumptions from the modding-kit headers. Form detection also
needs native combat-state fallbacks; `BP_IsNowAntiForm` alone is insufficient.
Keep executable/signature guards and weak lifetime identities intact.

Quiet updates consume posted changes; only active fades, pending work and
bounded form deadlines advance between events. Do not restore a broad music
poll or recurring directory scan. The runtime pool and exclusions are maintained
in `bgm_shuffle_table.cpp`; captured names alone do not establish loop or
duration behavior.

## Validation

[`tests/CMakeLists.txt`](../tests/CMakeLists.txt) registers
`bgm_shuffle_table`, `form_theme` and `custom_music_library` through its music
test loop. Build their targets before running CTest.

For authorized EGS live checks, build with diagnostics enabled using the
[native build guide](../README.md) and use an explicitly selected external
runtime test harness or manual gameplay checks.
Test restoration, pause/cutscene ownership, map changes and late decoder
completion when modifying those paths. Record actual playback/readback;
successful hook installation alone does not establish audible correctness.

Custom decoding uses one process-wide worker with at most one pending request per
playback channel. Superseded queued requests are discarded; canceled active work
checks its stop token before/after file reads and between Vorbis decode chunks.
The worker exits when idle and publishes through the existing completion event.
It holds no player or UObject pointer and does not join during DLL teardown.
`music_decode_queue` covers burst replacement, cancellation, failure and teardown.
