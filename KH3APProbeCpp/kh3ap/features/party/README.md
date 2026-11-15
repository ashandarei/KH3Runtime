# Party runtime

This feature owns companion selection, replacement, AI and native party observations.
Sora context and general player lifecycle services remain in `features/player/`.

- `party_member_service.h/.cpp`: service state, installation and AP unlock handling.
- `party_catalog.h`: member identities, default unlocks and custom AI definitions.
- `party_settings.cpp`: menu settings, choice availability and persistence.
- `party_roster.cpp`: desired/random pairs, vanilla roster capture and test snapshots.
- `party_reconciliation.cpp`: preload, delete, settle and spawn transactions.
- `party_ai.cpp`: scoped custom AI, presentation, knockout and maintenance.
- `party_swimming.cpp`: missing native swim locomotion, navigation and animations.
- `party_swim_combat.cpp`: scoped underwater attack settings and live AI state gates.
- `party_world_context.h/.cpp`: map policy and possession/Sora event observations.
- `party_helpers.h/.cpp`: reflected native operations and weak friend-slot reads.
- `party_map_policy.h`, `party_navigation_maps.h`, `party_pair_policy.h`: map,
  navigation and logical-to-physical pair rules.
- `party_runtime_constants.h`, `party_operation_timer.h`: shared contracts and timing.
- `party_test_service.h/.cpp`: diagnostic-only native stress bridge.

Include headers through `kh3ap/features/party/...`.

Catch-up applies to the two managed friend slots, including stock-AI choices.
Entry, relocation and battle-state changes wake native warp checks; native warp
restrictions and vanilla/solo map policies remain authoritative.

Swimming support fills missing locomotion, navigation and animations while
preserving authored movesets. Event-driven maintenance uses weak pawn identities
and bounded readiness retries, with no idle scan. Underwater combat adapts action
definitions, cached instances and owned live AI gates; exit or replacement
restores original values, including null queries and false magic flags.

Xion uses `BT_GoA_XionParty`. Aqua uses `BT_GoA_AquaParty` underwater and her
original `n_ex011_BT` on land. Both require matching GoA assets. Never nest Aqua's
stock tree in a RunBehavior wrapper; this caused GoA crashes. Flying attack
motion is not a water exit: unknown movement modes preserve the current context.
Aqua and Xion use native Curaga, and Kairi uses native Cura, with native healing
targeting and readiness checks.

Owned KH3 swaps retain all six native friend slots until possession is ready,
preserving the Vanilla baseline without rerolling. This ownership covers party
maintenance only; AP grants retain their Sora context and KH0.2 story worlds
remain excluded. See `features/player/character_party.*`.
