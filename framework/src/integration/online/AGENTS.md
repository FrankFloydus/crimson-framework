<!-- Copyright Francesco Di Blasi 2026 - All Rights Reserved -->

# Online game composition

- Keep the [directory contract](online_directory.hpp) shared across custom, Steam, and EOS providers. Run callbacks on the scene thread and settle requests asynchronously.
- Require verified identity before admission. Keep host admission closed until publication succeeds. Remove vendor listings and membership on cancellation, failed startup, and teardown.
- Keep Steam code in [steam/](steam/) and Epic code in [eos/](eos/). Build both through the same Crimson extension. Preserve SDK-disabled builds and keep SDK includes out of core Session.
- Use the [custom online fixture](../../../project/demo/tests/online_smoke.gd) for separate-process directory and identity checks. Use the [vendor guide](../../../vendor/README.md) for live provider prerequisites and export dependencies.
