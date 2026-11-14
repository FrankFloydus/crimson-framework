<!-- Copyright Francesco Di Blasi 2026 - All Rights Reserved -->

# SaveCoordinator API

Add a `SaveCoordinator` node to the active scene tree. Call configuration and request methods on the main thread. The first admitted ticket freezes the app, root, and storage and codec selection. The first admitted save or load freezes the scope, participant schemas, and migration registrations. The coordinator tracks host and adapter objects without owning them. Keep each binding alive while an active or queued save or load uses it.

## Configuration

| Method | Contract |
| --- | --- |
| `configure(app_id, root = "user://godot-save-system") -> bool` | Set the game’s stable app ID and initialize the default services. `root` is a nonempty directory under `user://`. Use separate roots for unrelated apps sharing a Godot user-data directory. |
| `configure_scope(scope_id, version) -> bool` | Set the scope and its current positive integer version. A coordinator has one scope, covering every registered participant. |
| `set_host(host) -> bool` | Bind or replace the object that builds and publishes the candidate world. Call when there is no active or queued save or load. |
| `declare_participant(id, version, required = true) -> bool` | Declare a unique stable ID and positive schema version without binding an object. `required` controls load behavior for a missing record. |
| `bind_participant(id, adapter) -> bool` | Bind or replace the live adapter for a declared ID. Call when there is no active or queued save or load. |
| `register_participant(id, version, adapter, required = true) -> bool` | Declare and bind a participant in one call. |
| `register_migration(id, from_version, callable) -> bool` | Register one step from `from_version` to `from_version + 1` for a registered participant. |
| `register_scope_migration(from_version, callable) -> bool` | Register one step over the array of `{id, version, data}` records. Scope steps run before participant migrations. |

Profile and slot IDs contain 1–64 lowercase ASCII letters, digits, underscores, or hyphens and start with a letter or digit. App, scope, and participant IDs also permit periods after the first character. Use durable gameplay IDs inside payloads. The default supports up to 64 participants and 256 registered migration steps across the scope and all participants. The storage backend also validates identifiers before resolving paths.

A title screen can call `configure` and `configure_scope` and then inspect slots without declaring participants or binding live objects. When gameplay is ready, declare the schema, register migrations, and bind the host and all adapters before the first save or load. When a scene is replaced, rebind the new objects under the same declared IDs. Keep any objects targeted by migration callables alive across scene changes.

Use one coordinator per independent scope and give each scope distinct slots or roots. The default store locates a slot by root, profile, and slot ID. Each coordinator orders its own requests. The host must coordinate capture and publication gates across coordinators that read or replace shared live state.

## Requests and signals

| Method | Contract |
| --- | --- |
| `request_save(profile, slot, metadata = {}) -> int` | Queue a manual save intent for the next host capture boundary. |
| `request_autosave(profile, slot, metadata = {}) -> int` | Queue an autosave intent. A newer intent supersedes an uncaptured autosave for the same slot and world epoch. Use a dedicated autosave slot if you want to preserve manual saves separately. |
| `capture_pending(revision = "") -> bool` | Synchronously capture the earliest pending uncaptured save. Call after finishing a gameplay transaction and before new mutations enter the checkpoint scope. The revision label is limited to 128 UTF-8 bytes. The method returns true when capture succeeds. |
| `request_load(profile, slot, sequence = "", generation_id = "") -> int` | Load the newest generation when both selection strings are empty. To load an explicit generation, pass both strings from the slot listing. |
| `request_prepare_load(profile, slot, sequence = "", generation_id = "") -> int` | Select a generation as in `request_load`, then read, migrate, stage, and link it. Wait for `load_prepared` before activating it. |
| `get_prepared_candidate(ticket) -> Variant` | Access the coordinator-owned candidate for a prepared ticket while it remains active. Returns `null` when the ticket has no accessible candidate. |
| `activate_prepared_load(ticket) -> bool` | Synchronously validate and publish the candidate on the main thread. A valid prepared ticket queues a terminal result. The return value is true if publication succeeded. |
| `validate_prepared_load_for_save(ticket) -> bool` | Validate and discard a prepared candidate requested by exact generation identity. The generation must still be the slot head when the worker verifies it. Success lets this coordinator save over that generation and retain it as a backup. The method queues a terminal load result and leaves the active world in place. |
| `request_list(profile, slot, before_sequence = "", before_generation_id = "", limit = 16) -> int` | Inspect committed generations, newest first. For the next page, pass both strings from the preceding result’s `next` cursor. The page size is positive and capped at 16. |
| `request_inspect_generation(profile, slot, sequence, generation_id) -> int` | Inspect one committed generation by exact identity. Use this to reconcile the generation reference returned by `CommitUnknown`. |
| `cancel(ticket) -> String` | Returns `Cancelled` if cancellation won, `TooLate` after the commit gate, or `UnknownTicket` for a ticket the coordinator no longer tracks. Wait for the terminal signal. |
| `invalidate_world() -> bool` | Invalidate work from the current world before replacing it outside the save system. If active work has passed its cancellation gate, wait for completion and retry. |
| `is_idle() -> bool` | Check whether work and terminal results are drained before changing or freeing the coordinator. |

Request methods return a positive ticket on admission and `0` on rejection. Read `get_last_admission_error()` immediately after a rejected request to distinguish codes such as `Busy`, `Unconfigured`, `InvalidArgument`, `RecoveryRequiresNewSlot`, and `CommitUnknown`. Read `get_last_admission_message()` for the reason or recovery guidance. Both values describe the most recent rejection and are cleared when a request is admitted. A rejected request produces no ticket events.

Synchronous setup failures also report their cause in Godot's console with a `[Crimson]` prefix. A failed reconfiguration retains the previously configured services.

The coordinator runs one background job at a time. Save, list, and exact inspection admission share a capacity of three jobs including the active job. A single pending load can extend this by one. A load supersedes uncaptured autosaves, waits behind previously admitted work, and fences new saves and loads. The host still needs to capture any admitted manual saves that precede the load, or cancel their tickets. Call `capture_pending` once for each save you want to capture at a safe point. A prepared load retains its place in the queue until the host activates or cancels it.

Signals are delivered on the main thread while the coordinator processes.

- `operation_progress(ticket, phase)` reports transitions such as `Queued`, `Capturing`, `Encoding`, `Writing`, `Publishing`, `Reading`, `Verifying`, `Migrating`, `Staging`, `Prepared`, and `Activating`. Some phases can be delivered in the same frame. Load verification progress is delivered before main-thread restore work so a listener can still cancel at `Verifying`.
- `load_prepared(ticket, generation)` reports that the explicitly requested load has staged and linked its candidate. The `generation` dictionary contains `sequence` and `id`. Call `get_prepared_candidate` if the host needs to prepare detached presentation or runtime state, then call `activate_prepared_load` at the host’s publication boundary. The host must keep a `Node` candidate outside the scene tree and without a parent until `save_publish`. Cancel the ticket if the host abandons the transition.
- `operation_finished(ticket, result)` reports the terminal result with `ok`, `code`, and optional `message`, `warning`, or `value`. A successful save or load returns `value = {sequence, id}`. After explicit activation, this signal arrives through the normal processing loop.

### Inspection results

A successful list returns the entries in `value`, the current slot-head `status`, `truncated`, and `next`. `truncated` is true when older entries remain. `next` is `{sequence, id}` for the last entry on the page, or `null` when no older entries remain. The next page starts at the first available sequence below the cursor. A slot with no generations reports `Empty`.

A successful exact inspection returns one entry in `value`. An absent generation returns `ok = false` and `code = "NotFound"`. Each entry contains `sequence`, `id`, and `status`. `Available` entries include `meta`, with `meta_truncated = true` and an empty `meta` if it exceeds the display budget. Other entry statuses are `Corrupt`, `Incompatible`, or `Unavailable` and include a diagnostic code and message. If a save reports `CommitUnknown` with a generation reference in `value`, inspect that exact reference. `Available` confirms it is present and verified, and `NotFound` confirms it is absent. Other outcomes leave the commit unresolved.

An `Incompatible` save result with `value.upgrade_source = {sequence, id}` identifies an older scope at the slot head. To save the current session into that slot, prepare a load with that exact identity. Call `validate_prepared_load_for_save(ticket)` when `load_prepared` arrives, then wait for its successful terminal result before submitting and capturing the save again. Migration, staging, linking, and host validation must all succeed. The coordinator discards the staged world and checks the current head again on write.

With no participant declarations, inspection validates the frame, app, scope and supported scope version, routing, generation identity, and the shape of participant records. With declarations, inspection also checks the current scope’s participant IDs and supported versions. In both modes `Available` reports inspection checks only. Migration, required data, and gameplay invariants are checked during load.

## Host and adapter callbacks

Callbacks run synchronously on the main thread. Keep gameplay mutations, signals, and external effects out of capture, migration, staging, and linking. Keep the active world available until the new candidate passes validation. The host owns its command gate and must allow observers to see the replacement only after publication succeeds.

| Callback | Owner and return contract |
| --- | --- |
| `save_capture() -> Dictionary` | Required adapter callback. Return `{"ok": true, "value": dto}` or a failure. The coordinator converts the DTO to detached data before returning from `capture_pending`. |
| `save_default() -> Dictionary` | Optional adapter callback. When a non-required participant is missing, return `{"ok": true, "value": default_dto}`. A missing required record or missing optional default fails the load. |
| `save_create_candidate() -> Dictionary` | Required host callback. Return `{"ok": true, "value": candidate}` for a fresh detached candidate. |
| `save_stage(dto, candidate) -> bool` | Required adapter callback. Validate the participant’s payload and populate only the candidate. |
| `save_link(candidate) -> bool` | Optional adapter callback. Resolve stable references after all participants have staged their data. |
| `save_validate_candidate(candidate) -> bool` | Optional host callback. Check aggregate invariants and finish fallible preparation at activation, including after host preparation of an explicitly staged candidate. |
| `save_publish(candidate) -> bool` | Required host callback. Activate the prepared candidate synchronously. A false return must leave the active world unchanged. |
| `save_discard_candidate(candidate) -> void` | Optional host callback. Release a candidate after a staging or activation failure, cancellation, invalidation, or removal of a coordinator holding a prepared load. When supplied, this callback owns cleanup. |
| `migration(old_data) -> Dictionary` | Return `{"ok": true, "value": upgraded_data}`. Use deterministic conversions. Missing links in the migration chain fail with `Incompatible`. |

Callbacks can include a reason by returning `{"ok": false, "message": "..."}`. The coordinator appends up to 512 characters of nonblank message text to the operation or participant context and retains the coordinator's error code. `save_stage`, `save_link`, `save_validate_candidate`, and `save_publish` accept literal `true` as success. Returning a failure from `save_publish` must leave the active world unchanged.

Capture and restore exchange data-only DTOs containing nulls, booleans, finite numbers, strings, arrays, and dictionaries with String keys. Encode integers outside ±9,007,199,254,740,991 as decimal strings. Encode engine-specific types explicitly in a DTO. Save metadata is bounded to 8 KiB. Other limits are listed in the [format guide](FORMAT.md).

Current-scope envelopes must use registered participant IDs and supported participant versions. An older scope can rename or remove records in its scope migration chain. The resulting records must satisfy the current scope before participant migrations run. Migrations upgrade accepted data in memory. A subsequent save can persist the upgraded snapshot as a new generation.

On successful publication the host owns the candidate. Otherwise the coordinator calls the discard callback if available. By default it schedules an unattached `Node` for deletion with `queue_free()` and releases its own reference to a `RefCounted` candidate. Supply `save_discard_candidate` to clean up a manually managed `Object` or host resources associated with the candidate. Keep ownership with the coordinator until publication and release any borrowed references when the ticket finishes.

## Save browser presentation

The native `SavePresentation` class builds bounded metadata for named saves, formats dates, and decodes thumbnail textures for inspection rows. Games can add their own display fields and date formats. See [Save browser presentation](PRESENTATION.md) for the static methods.

## Native extension seams

On Windows x86-64, `load_native_provider(path, configuration = "") -> bool` loads a trusted provider DLL after `configure` and before the first ticket. Pass an absolute DLL path or a Godot path that resolves to a real DLL on disk. The optional UTF-8 configuration is provider-defined and limited to 64 KiB. A provider can replace the store, codec, or both through the versioned C ABI. A replacement codec can still read reference GSAV frames through the built-in decoder. See the [native provider SDK](../sdk/README.md) for the header, example, and packaging requirements.

Custom C++ builds can also replace both defaults with `SaveCoordinator::set_services(std::shared_ptr<Store>, std::shared_ptr<Codec>)` after configuring the app and before submitting requests. Implement the interfaces in [save_core.hpp](../save_core.hpp). Store implementations own expected-head checks, cancellation before publication, and pruning. Codecs own bounded encoding and decoding of the detached JSON data model. Service operations run on the worker thread and must remain independent of live Godot objects.

The C++ overload `activate_prepared_load(ticket, diagnostic)` fills a Godot `String` with the activation failure or guidance after a successful backup restore. The asynchronous operation signal retains its usual contract.
