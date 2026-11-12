<!-- Copyright Francesco Di Blasi 2026 - All Rights Reserved -->

# Game Session

Read the [session contract](README.md) before changing lifecycle methods, signal order, or the admission RPC. See [identity and ownership](../../../docs/ARCHITECTURE.md#identity-and-ownership) for stable player bindings.

- Resolve the node's MultiplayerAPI and reserve it for one active session. Disconnect the session's signal callbacks during teardown. Restore the previous peer only if the API still holds the exact peer the session installed.
- Keep a client in `CONNECTING` until the current host confirms admission over the versioned authority RPC. Confirm against the owned transport and sender before publishing `CLIENT`. On the host, publish a roster addition only after admission succeeds. The resulting peer ID represents a connection. Game-specific authentication and player binding belong to the caller.
- Preserve the generation checks around lifecycle signals and initial roster enumeration. Signal listeners can leave or replace a session synchronously. Teardown clears the roster together and tree exit closes silently.
- Keep object decoding disabled for network sessions. Maintain matching relative RPC node paths across the peers' multiplayer scopes.
- Native immediate startup installs a host with admission already closed before any lifecycle signal. The orchestrator opens it after the world and local role are ready. Keep the standalone host method's open-admission default.
- Transport factories supply initialized Godot peers. Keep authority IDs, admission, the remote-seat bound, and API ownership in Game Session. Reserve the API during custom factory callbacks and verify the scope before installing the returned peer. Preserve the configured default ENet path and exact adapter errors.
- Verified admission runs on `SceneMultiplayer` while peers are pending. Keep challenges, proofs, async completion, and identities bound to the actual connection and session generation. Publish only after bilateral authentication. Preserve foreign auth settings and anonymous default behavior.

Use the [session tests](../../../project/tests/session/test_game_session.gd) for scope ownership, transport replacement, refusal, timeout, and teardown. The [separate-process ENet fixture](../../../project/tests/multiplayer_peer.gd) covers interaction with the Scene Loader. The [demo session fixture](../../../project/demo/tests/session_smoke.gd) uses scripted transport factories in separate processes and verifies that an admitted guest receives a reliable spawn denial before disconnect.
