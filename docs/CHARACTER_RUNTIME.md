# Character runtime maintenance

The native character implementation is maintained under
[`features/player`](../KH3APProbeCpp/kh3ap/features/player/). Executable identities,
native entry signatures, and platform status are recorded in
[`character_profiles.json`](../authoring/kh3/character_profiles.json).
The manifest and runtime guards determine which sites may run on each platform.
An offline signature match does not establish live gameplay compatibility.

After an intentional profile edit, regenerate and verify the shared table:

```powershell
python authoring/kh3/generate_character_profiles.py
python authoring/kh3/generate_character_profiles.py --check
```

The optional passive executable audit requires an explicit installation path.
It reads the selected executable on disk and does not launch or attach to KH3:

```powershell
python authoring/kh3/audit_character_profiles.py --egs-game-dir "$env:KH3_DIR" --output artifacts/character-audit.json
```

This command reads the process environment. If the path exists only in `.env`,
pass its value explicitly. Steam auditing requires an explicitly selected
`--steam-game-dir`; neither storefront is discovered automatically.

Run `scripts/test.ps1` after changing profile generation or character policy.
For behavior changes, separately validate the affected actions in a supported
live build, including switching back to Sora, map transitions, and object
lifetime handling. Preserve the fast UObject validity checks, weak handles,
executable guards, and pending-work cancellation rules.

## Recorded profile validation scope

The manifest records prior Steam checks of the four alternate characters:

- Fresh and cached character/camp/item swaps, including held-stick full and partial input.
- Swimming, diving, movement, descent, dash and resurfacing.
- Underwater ComboSwim attacks 118–121 with enemy HP readback and weapon collision groups.
- Focus retention, camera zoom, first-person photos, selfies and poses.
- Save-point interaction while moving and barrel spins.

These entries describe the scope of earlier validation, not a certification of
new DLL builds. Raw captures are not distributed with this project. The build
identities, per-site signatures and layout evidence remain in the manifest;
retain them when editing this summary.

Dedicated Steam coverage is still needed for wall-run equipment gates,
ground-slam hits, chest/ingredient/mast/ship interactions, shotlock damage
scaling and cinematic handoffs. Presented-frame performance on Steam is not
established by these checks; the low-level swap timing tracer is EGS-only.
Each changed DLL needs appropriate EGS regression checks before release.

## Priority reaction commands

Owned alternates route selected Talk, Check, Rescue and Gigas reactor commands
through Sora's native controller dispatcher. Cover Slide uses the same route
from Idle, Run or Cover, retaining native target selection. Riku has a separate
dispatcher and audited hook; ordinary Riku commands continue through his original function.
The selected actor/component, current world, local possession, active reactor
and command identity are checked on demand. Native eligibility and actor
callbacks still control completion. There is no idle actor scan or story-flag
override. Required world prompts using Check share this route; this does not
enable every combat situation command or certify every world's scripted scene.

Shared Talk, Rescue, Gigas and Cover states are available when an alternate's
authored state set omits them. Prepared traversal animsets include Cover motions
and Gigas entry/exit, with Sora's cover movement tuning. Trick accepts the original
Caribbean and Olympus acrobat barrels with their ODanceSpecialAct1 motion.

EGS arena checks on 2026-11-12 passed for Aqua, Riku, Kairi and Roxas:
one callback per Talk/Rescue/Check interaction; native Gigas boarding, movement
and L1 exit; both barrel Trick animations returning to Idle; and native Cover
Slide target selection, accepted command and movement into cover. Talk/Rescue
stations validate the reactor callback, not complete world-specific scenes.

The barrel paths retain the original handlers. Documentary native audit RVAs
(EGS / Steam) are reactor `0x11e0f70` / `0xf57120`, start `0x11e11d0` /
`0xf569f0`, end `0x11e1420` / `0xf56860`, RequestOneAction `0x1778260` /
`0x14415e0`, and performance broadcast `0x5f2540` / `0x45bda0`.
These addresses are evidence, not additional hooks.

## Crowd player ownership

`TresComNpcActionComponent::InitializeComponent` caches its local controller's
pawn in a private weak reference. Character restoration during map entry can
replace that pawn after the component initializes. Its tick then rejects the
stale reference and skips crowd spawning, leaving Corona town without villagers.
Pooled swaps can also leave a crowd following the parked outgoing pawn.

`character_crowd::rebind` refreshes that reference when the existing swap
transaction commits pawn ownership, including travel rebinds and returns to
Sora. This runs before restoring resource meters, so an unrelated restoration
failure cannot leave the crowd tracking the outgoing pawn. It discovers the
current world's authored crowd actors through `MyComNpcAction`, verifies local
possession and component ownership, and assigns a native weak reference. It adds no update
callback, timer, or retained raw UObject pointers. The `crowd_player_binding`
and `crowd_player_tick` profile sites verify the private `+0x338` weak-reference
layout against the selected executable before writing it.

The regression route is `/Game/Levels/ra/ra_02/ra_02`, start tag
`ra_02_Lv_Start_01`, with `gameflow_RA=RA_END`. Check crowd reference ownership
and live, non-hidden villagers after swapping in town, re-entering town as an
alternate, and returning to Sora. Use a fresh seed with the tested characters
precollected and record the original gameflow before a temporary story change.
Steam crowd profiles are matched offline; live Steam crowd validation remains
outstanding.

EGS validation on 2026-11-07 confirmed live crowd references and 42 non-hidden
villagers on town re-entry as Aqua, Kairi, Roxas, and Riku. In-town swaps and
returns to Sora also passed for Aqua, Kairi, and Roxas. Riku's separate
`character_magic_meter_restore` error still prevents a successful return-swap
test; crowd ownership and spawning remain correct despite that restoration
failure. This validation covers villagers, not Riku's resource restoration.

## Final Xehanort Ascension

The authored `bt_04_camera_assension:PersistentLevel.assention` movie's Play
and skipped-end-frame hooks return the controlled alternate to Sora before
binding its actors. The shipping
`TresEnemyPawn_e_ex301::BP_AscensionAttackStart` pre-hook shares that phase
transaction as a gameplay fallback. The
existing handoff preserves HP, MP, focus and party/target ownership; resource
restoration is attempted synchronously before the boss can change those values.
The selected alternate is retained for restoration. An active Mickey trap yields
to this scripted Sora requirement, while unapplied trap receipts stay queued.

While Ascension is active, manual input, menu selection, diagnostic selection,
queued combat swaps and cinematic/travel restoration cannot replace Sora.
Ending or skipping a movie does not release this restriction. The shipping
`BP_AscensionEnd` and `BattleEndAscensionMode` post-hooks release it only after
`IsAscensionMode` reads false. Restoration waits for normal control readiness
using the existing bounded transaction. Retry rechecks the mode once the new
player is ready; weak boss/world references retire the restriction on destruction
or world changes. The exact movie event resolves the normal Xehanort pawn once;
there is no idle actor scan or recurring phase query.

The pure Ascension state policy covers alternate targets, repeated entry/end
events, Sora-only entry, reentry before restoration and lifecycle cancellation.
Live regression coverage should exercise the native phase with Aqua, Kairi,
Roxas and Riku, R2/menu/queued swap rejection during the phase, skipped movies,
Retry and restoration after the boss exits Ascension. Passing the policy tests
and compiling the DLL do not establish that live coverage.

EGS validation on 2026-11-07 used the installed CoronaCrowd seed and native
Scala route at `bt_04_Lv_Start_02`, `gameflow_BT=BT_EVENT_bt905` (600).
Ordinary finishing attacks advanced the preceding boss into the native final
fight. Aqua was then selected through the normal experimental swap path, with
verified temporary protection. Two spaced 10% damage pulses reduced the final
boss from 1472 to 1172 and then 1172 to 872 HP; party damage subsequently
reduced him to 726 before the scripted phase.

The first run exposed that the gameplay callback occurs about seven seconds
after the movie starts. The revised movie hook committed the Sora handoff at
08:32:05 UTC; the gameplay callback observed Sora already present at 08:32:12.
Native style inspection then reported player 0, command 139 (`BP_IsNowAntiForm`'s
shipping Rage Form condition), and style level 1. R2 was consumed while the
lock remained active, and the player stayed Sora. Protection and surviving HP
were verified on the incoming pawn. Evidence is under the workspace's
`build/ascension/` directory. Phase-end Aqua restoration, Retry and skipped
Ascension movies remain live regression cases; their policy checks passed.
