<!-- Copyright Francesco Di Blasi 2026 - All Rights Reserved -->

# Online game composition

- Keep the [directory contract](online_directory.hpp) shared by Steam and custom providers. Run callbacks on the scene thread and settle requests asynchronously.
- Require verified identity before admission. Keep host admission closed until publication succeeds. Remove vendor listings and membership on cancellation, failed startup, and teardown.
- Run quick join as one bounded startup on the game's existing gate. Search results are advisory. Resolve the first compatible listing through normal verified admission, cancel the exact pending search on leave, and let the SDK lane drain. Apply invitation acceptance and dismissal to the exact pending offer from this attachment.
- Game lobby membership uses the Steam module's shared account reservation. The directory owns the Game route and admission state. Its scoped lobby facade observes only the exact membership installed by that directory.
- Steam listen and dedicated admission bind tickets to the actual certified transport identity and the exact SDK pipe. Hold a pending account reservation until its old callback can no longer settle a newer peer. The listen host rechecks current lobby membership before accepting Steam's result.
- Publish Steam Rich Presence join routes only from an eligible, ready Game with admission open. Preserve any foreign connect value and clear only the route written by the exact attachment.
- Keep the dedicated GameServer pipe separate from the signed-in client. Require current secure policy, certificate, world, and listener before publication. Only explicit Internet configuration enables Steam advertisement.
- Keep Steam provider composition in [steam/](steam/). Build it through the Crimson extension. Preserve SDK-disabled builds and keep Steamworks includes out of core Session.
- Use the [custom online fixture](../../../project/demo/tests/online_smoke.gd) for separate-process directory and identity checks. Use the [vendor guide](../../../vendor/README.md) for live provider prerequisites and export dependencies.
