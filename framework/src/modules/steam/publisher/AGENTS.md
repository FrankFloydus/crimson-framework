<!-- Copyright Francesco Di Blasi 2026 - All Rights Reserved -->

# Trusted Steam publisher services

- Keep publisher operations behind the explicit trusted build option. The ordinary game addon excludes these bindings and their optional encrypted AppTicket DLL.
- Resolve secret references in the attached game-owned host. Keep credentials, proof bytes, and raw backend response bodies out of logs and results.
- Use fixed Valve routes and verified HTTPS. Keep the local fixture transport under the separate test build flag.
- Validate the configured AppID and exact verified identity before acting for a player. Use the game's durable store to claim operation IDs, persist state transitions, and reconcile ambiguous outcomes before retrying mutations.
- Claim notification opt-in by trusted game launch and verified account across ticket renewals and backend workers. Release only claims proven unsent.
- Document every public binding in [trusted class XML](doc_classes/). Keep the [publisher guide](../../../../docs/STEAM_PUBLISHER.md) and deterministic fixture modes current.
