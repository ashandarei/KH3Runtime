# Native profiling and event-query contracts

Build with `-EnableDiagnostics` for timing and an explicitly selected live test
harness. Record DLL identity, separate fresh-process startup from warm captures
and compare returned gameplay values as well as timings. Scope durations include
children; do not sum nested scopes. Addresses do not identify objects across
processes/worlds. Microbenchmarks alone do not establish FPS gains.

## Keyblade style scheduling

Style policy reads immutable pak-backed `slot_data.stylesanity`, shared by checks
and grants. AP receipt validation and duplicate protection remain active. The
500 ms runtime pass validates pawn/controller and advances due cinematic/ascension
work. Equipment/style reflection runs when weapon, controller, ability, menu-close
or grant events mark state dirty, plus a five-second recovery check. Events get
one additional 500 ms settling check because notification can precede the write.
Initialization, deferred menu application and failed reads retain bounded retries.

`on_update:keyblade_style_sanity` includes settings/context lookup as well as
style work. Older captures measured only inner work; compare enclosing hitch
phases when crossing that instrumentation change. `keyblade_style_polling_test`
covers mode selection and event/settling/recovery scheduling without a game.

## Reflection and selected reads

[ReceiverFunctionCache](../KH3APProbeCpp/kh3ap/core/reflection/receiver_function_cache.h)
resolves exact function paths through the receiver's class hierarchy. Its bounded
weak class/function cache preserves specified base functions against overrides
and retries misses on the next call. Enum setup also caches weak identities.
Equipped weapons, flags and ability ownership remain live values.

Diagnostic scopes separate weapon reads, enum resolution, controller booleans,
quality-of-life flag rules, getter layout lookup and ability-bank validation.
Object-field metadata caches validate property type/size under weak class identity;
class replacement invalidates them. Current object values and reciprocal
controller/pawn possession are still read and validated on every access.

Magic/Links reads ability IDs 115 and 134 together; Aqua's shared unlock refresh
reads 113-116 and 134. Selected reads preserve requested order and duplicates,
reject invalid ranges and clear all outputs on failure. They delegate to the
validated contiguous reader, retaining supported-image, save-owner, pawn/equipment,
active/staging-table and exact-byte-count checks. Retain no bank address or
ability state across events, and preserve setter order/restriction policy.

Benchmark separate and batched queries in the same process with equal returned
ownership/equipment state, including agreement with the four-entry controller
snapshot. Testing the five-entry pattern on Sora does not establish Aqua's live
behavior. Standalone coverage includes selected-range boundaries, duplicate IDs,
changed live values and failed/partial reads.

## Command rebuilds and display bindings

Magic refresh reads adjacent bonus counts 24-29 from the validated active record.
Failed/short reads fail refresh. Skip native manager setup when all counts are
zero; otherwise retain bonus order and native signature validation. Link refresh
queries all five Heartbinders first: no ownership needs no manager, but query
failure remains retryable. Owned commands preserve registration order/routine.
Snapshots and native addresses are local to the event.

The large map uses bulk display-info reads for player/canvas geometry with a
member-read fallback for unsupported layouts. Diagnostics compare pixel position
and heading against the original reads. See the [overlay contract](../KH3APProbeCpp/kh3ap/features/ui/map_overlay/README.md)
for projection, invalidation and layer lifecycle. Validate populated/empty command
paths separately, plus travel, both maps and viewport resize/restoration. Keep
native service timings distinct from rendering and end-to-end input latency.
