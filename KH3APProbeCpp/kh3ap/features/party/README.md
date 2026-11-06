# Party runtime

This feature owns companion selection, replacement, AI and native party observations.
Sora context and general player lifecycle services remain in `features/player/`.

- `party_member_service.h/.cpp`: service state, installation and AP unlock handling.
- `party_catalog.h`: member identities, default unlocks and custom AI definitions.
- `party_settings.cpp`: menu settings, choice availability and persistence.
- `party_roster.cpp`: desired/random pairs, vanilla roster capture and test snapshots.
- `party_reconciliation.cpp`: preload, delete, settle and spawn transactions.
- `party_ai.cpp`: scoped custom AI, presentation, knockout and maintenance.
- `party_world_context.h/.cpp`: map policy and possession/Sora event observations.
- `party_helpers.h/.cpp`: reflected native operations and weak friend-slot reads.
- `party_map_policy.h`, `party_navigation_maps.h`, `party_pair_policy.h`: map,
  navigation and logical-to-physical pair rules.
- `party_runtime_constants.h`, `party_operation_timer.h`: shared contracts and timing.
- `party_test_service.h/.cpp`: diagnostic-only native stress bridge.

Include headers through `kh3ap/features/party/...`. The target's CMake source list
and the maintenance-test generator reference this directory explicitly.


Owned KH3 swaps use `features/player/character_party.*` to retain all six native
friend slots until incoming possession is ready. The party owner resolver admits
those presentation pawns for roster/AI maintenance only; AP grants retain their
existing Sora context. The handoff observer carries the Vanilla baseline without
rerolling, and KH0.2 story worlds remain excluded from this ownership path.
