<!-- Copyright Francesco Di Blasi 2026 - All Rights Reserved -->

# Game Session

`GameSession` is a persistent Node that owns a multiplayer transport or an explicit offline session. Network starts use ENet by default. Put it under the same MultiplayerAPI scope as the nodes that exchange RPCs, including the scene load manager. Host and client session nodes must have the same path relative to their respective MultiplayerAPI roots. The node works with the scene tree's default API or a subtree API assigned with `SceneTree.set_multiplayer`. Use the scene tree's normal multiplayer polling or poll the configured API yourself when automatic polling is disabled.

Use one active Crimson session per MultiplayerAPI. Starting a session claims an idle API and remembers its existing offline peer or null value. Ending the session restores that peer if Crimson still owns the slot. Removal from the tree closes the session silently. If another system replaces the API or peer, Crimson reports `transport_replaced` and leaves that replacement installed.

## Methods

| Method | Contract |
| --- | --- |
| `start_offline() -> Error` | Install an offline peer. The local session is authoritative and has ID 1. |
| `host(port, max_clients = 32, bind_address = "*") -> Error` | Listen using the selected transport. The port is 1–65535, the client limit is 1–4095, and the bind address is `*` or a literal IP. Peer 1 is authoritative. |
| `join(address, port, timeout_seconds = 10.0) -> Error` | Connect to a direct host address. The timeout is positive and at most 120 seconds. Wait for `session_started` before sending gameplay RPCs. |
| `join_endpoint(endpoint, timeout_seconds = 10.0) -> Error` | Connect using a `SessionEndpoint` accepted by the selected adapter. Use `SessionEndpoint.direct(address, port)` for an IP address or hostname. |
| `set_transport_adapter(adapter) -> Error` | Select a `SessionTransport` while idle. Pass `null` to restore ENet. Offline startup uses Godot's offline peer. |
| `set_authenticator(authenticator) -> Error` | Select a `SessionAuthenticator` while idle. Pass `null` to use the scope's existing authentication policy. |
| `get_authenticator() -> SessionAuthenticator` | Read the configured authenticator, or `null`. |
| `get_peer_identity(peer_id) -> SessionIdentity` | Read the verified identity for an admitted direct connection, or `null`. The authority can read each verified guest. A guest can read the authority. |
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
- `session_ended(reason)` reports `left`, `connection_failed`, `connection_timeout`, `protocol_mismatch`, `server_disconnected`, `transport_replaced`, or `transport_disconnected`. Verified sessions can also report `authentication_failed` or `authentication_replaced`.

All lifecycle calls and signals run on the scene thread. The scene load manager can close admission through the shared host peer during a scene transition. Reopen admission only when the game can synchronize a joining player with the active world.

## Custom transports

Games keep the standard call `await game.host_game().wait()`. For custom transport configuration, subclass `GameServiceProvider`, return a fresh detached session from `_create_session()`, and select the adapter before returning the session. Set the provider with `definition.with_provider(provider)` before adding the game to the tree. The injected session serves both immediate and coordinated play.

```gdscript
extends GameServiceProvider

func _create_session() -> GameSession:
    var session := GameSession.new()
    session.set_transport_adapter(MyTransport.new())
    return session
```

`MyTransport` extends `SessionTransport` and implements these hooks.

| Hook | Contract |
| --- | --- |
| `_open_server(port, remote_limit, bind_address) -> SessionTransportResult` | Return a fresh initialized server peer with ID 1 and the requested remote-seat bound. The session sets admission before installing it. |
| `_open_client(endpoint) -> SessionTransportResult` | Return a fresh initialized client peer. It can still be connecting. It must have an ID greater than 1 by the time it receives admission. |
| `_disconnect_after_flush(peer, peer_id)` | Drain already queued reliable messages for the given admitted guest, then disconnect. The default hook requests a regular Godot disconnect. Implement it when a protocol relies on receiving its terminal denial before closure. |

Return `SessionTransportResult.opened(peer)` on success and `SessionTransportResult.failed(error)` on failure. An absent open hook reports `ERR_UNAVAILABLE`. The native `ENetSessionTransport` implements the hooks and exposes `open_server`, `open_client`, and `disconnect_after_flush` for composition. `SessionEndpoint` exposes read-only direct values through `get_address()` and `get_port()`.

A directory or adapter can use `SessionEndpoint.opaque(transport, route_bytes)` when its route has no IP address or UDP port. The adapter name uses 1–32 lowercase ASCII letters, digits, or underscores and starts with a letter. `enet` is reserved for direct endpoints. The route is 1–512 bytes of adapter-owned routing data. Read the adapter name and a copy of the bytes with `get_transport()` and `get_route()`. Carry authentication separately. ENet rejects opaque endpoints.

Factories run on the scene thread. Return an uninstalled peer dedicated to this session, with the normal Godot peer signals and polling behavior. Deliver connection events only after installation. Implement reliable ordered delivery on channel 0 and honor the host's refusal flag. The session owns attachment, admission confirmation, the admitted-peer bound, and cleanup. It closes an unused peer if activation fails and preserves a scope replacement made during the open call. Concurrent setup on the same MultiplayerAPI is rejected. Adapters are trusted local game code. Their endpoint and peer IDs identify a connection. Platform identity and authentication require a separate trusted provider or authenticated MultiplayerAPI. [Godot MultiplayerPeer](https://docs.godotengine.org/en/4.7/classes/class_multiplayerpeer.html) · [Godot MultiplayerPeerExtension](https://docs.godotengine.org/en/4.7/classes/class_multiplayerpeerextension.html)

## Verified admission

Select an authenticator in the same `_create_session()` factory before attaching the game. Use it on both ends of a verified connection. `GameSession` uses `SceneMultiplayer` to hold each transport connection pending until both ends complete authentication. The scope needs an unclaimed `auth_callback` and an existing positive `auth_timeout` of at most 30 seconds. Session preserves the timeout and restores the callback when it still owns the slot. Anonymous direct sessions and offline play keep their previous behavior. [Godot SceneMultiplayer](https://docs.godotengine.org/en/4.7/classes/class_scenemultiplayer.html)

| Authenticator hook | Contract |
| --- | --- |
| `_make_proof(peer_id, authority, challenge) -> PackedByteArray` | Build a proof synchronously from prepared local credentials. `authority` describes the local process. The peer supplied the 32-byte challenge. Return 1–4096 bytes, or an empty array to deny. |
| `_verify(request)` | Verify the remote proof against its provider, challenge, and session or endpoint context. Use `request.get_peer_id()`, `is_authority()`, `get_challenge()`, and `get_proof()`. Complete synchronously or later on the scene thread with `request.accept(SessionIdentity.verified(provider, subject))` or `request.reject()`. |

The provider namespace and stable account subject identify the verified remote account. They are bounded to 64 and 256 characters respectively. Resolve them from verified provider evidence. Provider hooks run on the scene thread outside the engine packet callback. A pending request can settle once and expires when that connection or session ends. The authority can read the identity from `peer_joined(peer_id)`. Leaving or disconnecting retires that binding. Guest roster entries relayed by Godot do not expose those other guests' identities through this API.

Native framing bounds pending work and associates each proof with one engine connection and a fresh challenge. The authenticator supplies the verification policy, including binding the proof to the challenge and selected host or session, verifying provider tickets, and protecting reusable credentials. Use an authenticated, encrypted transport when the selected proof or deployment needs confidentiality. Keep proof bytes out of logs and persisted session data. A successful transport or directory lookup by itself grants no gameplay admission.
