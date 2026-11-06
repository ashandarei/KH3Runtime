# Native item delivery and reconciliation

Shared grant safety, pawn state and lifecycle live at this feature's root.
`delivery/` owns binding/execution/receipt state; `abilities/`, `vbonus/`,
`keyblades/`, `messages/`, `events/` and `battle_level/` own specific operations.
The [bridge reconciliation worker](../../bridge/rewards/reconciliation.cpp)
owns scheduling and durable receipt membership.

## Seed and receipt ownership

The installed pak supplies immutable seed options, location/reward catalogs and
seed/player identity. Composition binds that document source before constructing
services. Shared JSON reads route seed documents there; AP synchronization and
mutable progress use the bridge. `SeedDocument` views retain ownership through
aliasing shared pointers, without serializing/reparsing nested rows. Standalone
file readers retain their metadata/size/validation boundary; existing views stay
bound to their original document.

The client publishes AP receipt IDs and server-check facts through
`kh3ap.receipt_catalog.v4`. The native worker owns progressive/bundle expansion,
natural-local acknowledgement, forced-local delivery, Event/VBonus repairs,
final checks and in-game notification decisions. `bridge/rewards/catalog.cpp`
decodes rewards and classifications. Shared IDs/tokens/flag rows are generated
from `data/runtime-inputs.json`; configuration rejects stale generated headers.

Event/VBonus/chest services read the pak-backed seed and shared catalog directly.
`runtime_check_state.json` carries identity-bound final checks and goal state.
The DLL observes ending-world transitions and expands gated, ranked, aggregate
and triggered checks. `kh3ap.native_reconciliation.v2` durably stores that state
and save/retry membership. Newly generated seeds are the supported target.

Native loot observations choose durable repairs; inventory members are excluded
from those repairs. Ordinary AP replay retains the complete reward bundle.

## Scheduling and execution

Reconciliation derives one delivery schedule per publication. Pending receipts
still gate presentation while gameplay is blocked. Receipt projections compare
all typed inputs before encoding; save previews include membership, eligibility,
descriptions and errors. Acknowledgement history participates even when active
membership is unchanged. Preserve durable commits and save/retry event ordering.

`item_grant_poll_helpers.cpp` owns request selection/arming and the early map gate.
`item_delivery_context.cpp` applies ordered safety gates and current Sora/space
context. `item_delivery_execution.cpp` adapts native grants, HUD presentation and
acknowledgement writes; `item_delivery_diagnostics.cpp` owns reporting.

`item_delivery_flow.cpp` has no Unreal dependency. It owns batch permissions,
fresh-delivery identity, ordered execution, partial outcomes and acknowledgement
transitions. Remember successful fresh grants before acknowledgement I/O; replay
bypasses that dedupe. Ordinary failures permit later grants, while unsafe failures
stop the batch. Consume requests only after successful acknowledgement, or when
already acknowledged. Batch observers run after each applied grant; single-request
observers run after successful acknowledgement.

Notifications wait in a native FIFO for the Sora/UI safety gate. Load/Retry drops
messages from the previous delivery epoch. `runtime_notification_event.json`
separately carries selected text to client history; consumption schedules no
additional gameplay action.

## Validation

Run `scripts/test.ps1` from the runtime root. Native service/policy tests cover
real decoders, receipt binding, projection invalidation and retry behavior with
injected engine effects. `test_kh3_runtime_item_delivery` exercises the standalone
flow. Verify live context safety and save/retry behavior separately; unit tests
and reduced JSON work do not establish a CPU/FPS gain. See
[profiling](../../../../docs/PROFILING.md) for event-query boundaries.
