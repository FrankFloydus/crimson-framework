<!-- Copyright Francesco Di Blasi 2026 - All Rights Reserved -->

# Steamworks

- Use the [Steam architecture and verification guide](../../../docs/STEAMWORKS.md) for ownership and per-feature test requirements. Keep standalone services independent of `src/integration`.
- Share one process client runtime and one main-thread callback pump. Invalidate service handles when their scope stops. Keep SDK-off class registration and explicit unavailable results.
- Add or update [class XML](doc_classes/) for every changed public Steam binding. Document methods, properties, enum values, and signals in Godot Help.
- Omit empty XML sections or use explicit opening and closing tags. Godot 4.7's Help loader can reject a self-closing section followed by a method or member section.
- Register the shared Steam icon in the [extension descriptor](../../../project/addons/crimson-framework/crimson-framework.gdextension) when adding a Steam node or editor-facing configuration resource.
- Keep SDK handles native. Copy borrowed callback data before returning it to Steam. Release handles on success, failure, and teardown.
- Use the same exact Steam account reservation for standalone social and Game lobby membership. Drain abandoned native acquisitions before another scope can take membership. Keep social publication metadata distinct from Game routes. Lobby chat copies a bounded payload before Steam releases the callback's chat index.
- Keep game-owned lobbies and authority in [online integration](../../integration/online/AGENTS.md). Keep publisher credentials in trusted backend tools.
- Use the [Steam testing guide](../../../docs/STEAM_TESTING.md) for standalone, rendered, multiplayer, and trusted publisher fixtures. Keep private app and account mutations behind explicit test actions.
