<!-- Copyright Francesco Di Blasi 2026 - All Rights Reserved -->

# Network

The module provides committed-state replication and a typed binary codec for explicitly encoded gameplay messages. Fixed-world movement uses the native codec for its input and pose records. Coordinated games continue to use recipient-specific committed views.

## Typed binary messages

Use `NetworkWriter.create()` and `NetworkReader.open()` for a short, fixed message layout. Write and read fields in the same order. `finish()` on the reader verifies complete consumption. Check the writer's error before sending its finished bytes.

For a game-defined type, register its encode and decode callbacks once through `game.codecs()` with a stable name and version. Use the same registration on both peers. The registry runs the selected codec locally and returns a typed result. The game chooses how and when to send the bytes and supplies the codec ID and version when it decodes them. The codec bytes contain only the fields the callback writes. A standalone `NetworkCodecs` instance can serve code that uses the module independently.

The native [wire codec](wire_codec.hpp) writes to caller-owned memory and reads directly from the supplied buffer. It uses fixed-width little-endian integers, finite IEEE-754 floats, canonical booleans, and uint32-length-prefixed byte fields. It performs no heap allocations. The Godot wrappers own their buffers and perform the conversions needed for script values. Movement calls the native codec directly.

The [Release benchmark](../../../docs/NETWORK_SERIALIZATION_BENCHMARK.md) measures native encode and decode, Godot RPC framing and fan-out, and the cost of publishing a committed view.

## Committed state

`StateReplication` sends bounded, recipient-specific committed views over Godot's current `MultiplayerAPI`. Put one persistent node at the same path relative to the multiplayer root on the server and each client. Keep the node's authority at server peer 1 and keep Object decoding disabled in the multiplayer scope.

The game provides a synchronous projector. It reads the indicated committed revision and returns `{"ok": true, "view": records}`, where `records` contains only what the given player may see. The server supplies the admitted player identity to this callback. A missing or rejected projection fails synchronization for that recipient. A projector may return `{"ok": false, "message": reason}` to explain a rejection. The authority prints the bounded message locally. The node has no dependency on the session, state, inventory, or scene loader modules.

## Authority API

| Method | Contract |
| --- | --- |
| `set_projector(callback)` | Bind a callable receiving `(peer_id, player_id, world_epoch, committed_revision)`. Configure it before registering peers. Keep the callback alive and free of side effects. |
| `register_peer(peer_id, player_id, binding_generation)` | Register a connected, already admitted peer. Binding generations are positive. A newer generation replaces the peer's prior stream. |
| `advance_world(world_epoch, committed_revision)` | Set a strictly newer, positive epoch and a nonnegative revision. Registered bindings survive and their readiness is cleared. |
| `send_baseline(peer_id)` | Send a fresh snapshot once the peer has activated the matching scene. Wait for `peer_ready` before opening the peer's command gate. Immediate errors are returned. |
| `publish_changes(committed_revision)` | Advance to a nondecreasing revision and send upserts and removals to ready peers. Pending baselines catch up on acknowledgment. Hidden-only revisions leave a recipient's delivery sequence unchanged. |
| `is_peer_ready(peer_id, binding_generation = 0)` | Check whether the connected peer has acknowledged this epoch's baseline. Pass the admitted binding generation when gating input. |
| `remove_peer(peer_id, binding_generation = 0)` | Remove the stream. Supply a nonzero generation to guard against stale disconnect events. Transport disconnect also removes the stream. |
| `reset()` | Drop active streams, revision, and epoch. Retain the projector. The scope or transport changing also clears active streams. |

`peer_ready(peer_id, world_epoch, binding_generation)` follows a matching acknowledgment and successful catch-up. `peer_sync_failed(peer_id, world_epoch, error)` reports a baseline acknowledgment timeout or an asynchronous delivery failure. Readiness stays closed until synchronization succeeds. Game Flow can retry with a new baseline, remove the peer, or recover the match.

## Client API

- `projection_updated(world_epoch, committed_revision, baseline)` reports a newly applied baseline or delta. Read the view with `get_projection()`, which returns a detached dictionary.
- `is_client_ready()` reports whether a live, continuous view is available. `get_world_epoch()` and `get_revision()` report the last accepted cursor while connected.
- `clear_projection()` invalidates the visible view without forgetting the current stream identity. A scene orchestrator can call it after activating a new world and before receiving its baseline. `projection_cleared()` reports invalidation.
- A missing or invalid delta invalidates the view and requests a fresh baseline. The console identifies the cause and reports if the request could not be sent. The authority gates further input from the peer while a replacement baseline is pending.

## View and delivery bounds

Use a dictionary keyed by stable, nonempty String record IDs up to 128 codepoints. Each value may contain null, Boolean, integer, finite floating-point, String, Array, and Dictionary values with String keys. Encode engine values in the game adapter. A view accepts up to 16,384 DTO nodes including keys, 65,536 total string codepoints, and depth 20. Each recipient sees removals when a record leaves its view. Sequences, binding generations, and baseline identifiers are transient protocol metadata.

Replication uses reliable ordered RPCs on channel 0. Baseline acknowledgment checks the current sender, binding generation, epoch, and baseline ID. The default deadline is 15 seconds. The server projects a complete view per ready peer for each publication, then transmits only changes to that peer. Use this path for committed gameplay state and bounded views. The game can synchronize high-rate movement through its own transport and interpolation policy.
