# Native startup and save-load contract

The diagnostic [autoload service](../KH3APProbeCpp/kh3ap/features/world/startup/autoload_service.cpp)
runs on the identified game thread before the title-screen fast path. It uses
weak UObject identities and `kh3ap::is_uobject_valid_fast`. Normal player builds
exclude the agent handlers. The opted-in KH0.2 core runtime separately owns the
guarded native load gate, including in diagnostics-off builds.

## Bridge protocol

| File | Purpose |
| --- | --- |
| `runtime_autoload_request.json` | Atomically published request, consumed once. |
| `runtime_autoload_result.json` | Latest request state. |
| `runtime_autoload_events.jsonl` | Diagnostic state history. |

Requests carry a fresh `request_id`, `command`, `process_id` and future
`expires_unix_ms`. PID 0 is reserved for requests queued before launch. The host
serializes requests with a bridge lock; an unexpired pending request also blocks
another load. Terminal evidence must match both request and process identity.

| Command | Additional fields/behavior |
| --- | --- |
| `load_autosave` | Explicit slot 0. |
| `load_slot` | `slot` in 1-99. |
| `continue_native` | Game chooses newest save from its in-memory catalog. |
| `inspect_startup` | Snapshot only; terminal phase `completed`. |
| `wait_gameplay` | Current PID required; optional `expected_map`. |
| `save_slot` | Current PID; authorized test slot 50-99; EGS only in this service. |

`defer_kh02_preparation: true` tests the native gate on explicit slot/autosave
loads; it requires an installed gate on a supported storefront. Results use
`kh3ap.runtime_autoload.v1` with phases `accepted`, `waiting`, `loading`, `ready`,
`completed` and `error`. Steam rejects this service's `new_game`, `setup_startup`
and `save_slot` commands; native Continue and gate tests support both storefronts.

Inspect `reason`, `observed_slot`, `title_state`, `load_state`, `load_result`,
`autosave_available`, `pawn`, `map_ready`, `blocking_ui`, `input_enabled`,
`loading_continue_dispatched`, `loading_transition_complete`,
`loading_screen_closed`, `loading_prompt_ready`, `loading_screen_mode` and
`loading_screen_progress`. Dispatch is an intermediate state. Prompt readiness
normally returns to false after completion.

## Selection and preparation

Explicit loads and native Continue require an idle manager and initialized
catalog at title state 14 or 19. Continue runs the original newest-save selector;
explicit loads retain their requested slot. The enabled KH0.2 gate prepares
source providers/aliases only for imported destinations, retaining and then
releasing the same native completion. Ordinary saves bypass that preparation.

An idle title manager can still contain an old selected-slot value. Confirm
`observed_slot` only after loading begins or title has been left. Reject the
first mismatching explicit selection; preserve a verified selection through
manager resets. Continue must observe a real slot in 0-99.

Snapshot the gate's failure sequence before dispatch. Only newer failures with
a reason terminate that request; an old failed attempt must not poison a later
load. Native result 1 and title state 25 are success stages; results above 1
remain failures. Timeout cannot cancel a load already dispatched.

Relevant failures include `unsupported_executable_build`, `autosave_unavailable`,
`already_in_gameplay`, `native_save_load_failed`, `process_id_mismatch`,
`native_load_gate_failed: <reason>`, `native_slot_selection_mismatch`,
`native_slot_selection_unobserved`, `native_continue_catalog_unavailable` and
`native_continue_selector_profile_mismatch`. Preserve evidence without an
automatic retry or error-dialog action.

## ABI and loading completion

[autoload_profile.h](../KH3APProbeCpp/kh3ap/features/world/startup/autoload_profile.h)
and the service own executable identities, offsets, signatures and layouts.
Keep numeric profiles there rather than maintaining a second RVA table in prose.
Unknown images/layouts fail closed. Re-derive the ABI and verify a cold launch
when changing a profile; do not relax guards to admit another binary.

The service supplies the game's own completion delegate to the asynchronous
slot loader. Steam uses a distinct loader prologue and title/GameOver delegate
implementations. Its loading-task action is a one-entry multicast list; EGS's
single-delegate layout must not be applied to it. Gate validation checks whole
stolen instructions, remaining original bytes and trampoline return targets.

After title departure, wait for the loading thread and prompt-ready operation.
The progress gauge must be valid in [0, 1], but need not reach 1. Validate screen
ownership, singleton vtables and the bound predicate/completion task, invoke its
action once, then retire it through the native scheduler under its task-map lock.
Closing the loading screen alone is insufficient; the shipping reflected close
thunk is empty. Readiness requires the operation inactive, task/presentation
pointers released, active/pending task maps empty and stable playable ownership.
Resolve native pointers per game-thread tick; retain no unguarded UObject.

## Validation

Run `scripts/test.ps1`; the autoload profile policy tests cover selection changes,
stale/empty failure evidence and per-request isolation. A live cold load must
also prove selected-slot identity, loading completion, closed UI and stable
playable control. Gate-retained/ready logs or bootstrap generation evidence
establish cold-provider preparation. Test imported worlds and GameOver retention
separately; ordinary save loading does not establish either. No validation path
requires inspecting vanilla save-slot files or attaching a debugger.
