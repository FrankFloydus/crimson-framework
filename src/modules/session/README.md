<!-- Copyright Francesco Di Blasi 2026 - All Rights Reserved -->

# Game Session

`CrimsonGameSession` is a persistent Node that owns an ENet transport or an explicit offline session. Put it under the same MultiplayerAPI scope as the nodes that exchange RPCs, including the scene load manager. Host and client session nodes must have the same path relative to their respective MultiplayerAPI roots. The node works with the scene tree's default API or a subtree API assigned with `SceneTree.set_multiplayer`. Use the scene tree's normal multiplayer polling or poll the configured API yourself when automatic polling is disabled.

Use one active Crimson session per MultiplayerAPI. Starting a session claims an idle API and remembers its existing offline peer or null value. Ending the session restores that peer if Crimson still owns the slot. Removal from the tree closes the session silently. If another system replaces the API or peer, Crimson reports `transport_replaced` and leaves that replacement installed.

## Methods

| Method | Contract |
| --- | --- |
| `start_offline() -> Error` | Install an offline peer. The local session is authoritative and has ID 1. |
| `host(port, max_clients = 32, bind_address = "*") -> Error` | Listen on ENet UDP. The port is 1–65535, the client limit is 1–4095, and the bind address is `*` or a literal IP. Peer 1 is authoritative. |
| `join(address, port, timeout_seconds = 10.0) -> Error` | Start connecting to an ENet host. The timeout is positive and at most 120 seconds. Wait for `session_started` before sending gameplay RPCs. |
| `leave()` | End the current session, including a pending connection. Repeated calls while idle have no effect. |
| `get_state() -> State` | Return `IDLE`, `OFFLINE`, `HOSTING`, `CONNECTING`, or `CLIENT`. |
| `is_authority() -> bool` | True during offline and host sessions. Authority for individual networked nodes still follows the project's Godot authority assignments. |
| `get_local_peer_id() -> int` | Return the connected local ID or 0 while idle or connecting. |
| `get_connected_peer_ids() -> PackedInt32Array` | Return the sorted remote IDs observed in this API scope. Godot's relay configuration determines which other peers a client sees. |
| `set_accepting_connections(enabled) -> Error` | Set the active host peer's admission flag. Use it to close the lobby without disconnecting current peers. |
| `is_accepting_connections() -> bool` | Read the active host peer's admission flag. Returns false in other roles. |

Method errors describe immediate validation or transport setup. A client stays in `CONNECTING` until the host confirms admission over a reliable authority-only RPC. This covers the case where the ENet handshake succeeds locally but the host has already closed the lobby. A network start rejects a standard SceneMultiplayer with object decoding enabled. Keep object decoding disabled on custom APIs as well. Games can configure their own authentication, participant permissions and persistent player identities. A peer ID identifies a connection and can change on reconnect.

## Signals

- `state_changed(state)` reports lifecycle changes, including the pending connection.
- `session_started(state, local_peer_id)` reports an active offline, host, or client session.
- `peer_joined(peer_id)` and `peer_left(peer_id)` report remote changes during the active session. Ending a session clears the roster in one step.
- `session_ended(reason)` reports `left`, `connection_failed`, `connection_timeout`, `protocol_mismatch`, `server_disconnected`, `transport_replaced`, or `transport_disconnected`.

All lifecycle calls and signals run on the scene thread. The scene load manager can close admission through the shared host peer during a scene transition. Reopen admission only when the game can synchronize a joining player with the active world.
