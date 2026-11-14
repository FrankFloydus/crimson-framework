<!-- Copyright Francesco Di Blasi 2026 - All Rights Reserved -->

# Crimson workspace

- [Framework sources, build, demo, and test guides](framework/AGENTS.md)
- [Public API](framework/docs/PUBLIC_API.md) and [architecture](framework/docs/ARCHITECTURE.md)
- [Framework work log](framework/LOG.md) and [release procedure](framework/releases/README.md)
- [Mintlify documentation](docs/) lives in its own Git checkout. Follow its [local instructions](docs/AGENTS.md).

The user decides the authoring workflow for each feature. Ask the user when that decision is needed and they have not supplied it.

## Write documentation for Crimson users

- Start with the reader's goal. Explain what the feature does, why a game would use it, and when it is optional. Use short sentences and everyday language. Explain a necessary technical term when it first appears. On the home page, describe what Crimson helps developers build. Introduce classes and method calls in the relevant guides.
- Write installation for customers using the purchased addon ZIP. Keep source builds and GitHub downloads out of the customer path. Base descriptions and examples on the implemented bindings and the matching addon build. Use the current demo for examples. Keep compatible older APIs in migration and lower-level reference.
- Keep **Docs** and **API Reference** as the two top-level tabs. In **Docs**, group guides by domain. Introduce the main concepts and classes, then offer a small working example before advanced topics. Inventory starts with a visible, usable backpack and progresses to drag and drop, other inventory operations, and world pickups. **Steam Integration** starts with what the module does, then a minimal Steam connection check, then app services and multiplayer. Keep Steam launch prerequisites with the relevant setup.

## Teach Crimson through runnable examples

- Make the solo and multiplayer quickstarts independent. State the required scenes, nodes, and settings briefly. Walk through the chosen setup and show how to tell it worked. Give each separate example its own setup. Link to Godot's docs for generic editor procedures.
- Explain what game boot prepares and what starts when the player acts. For solo, explain why a single-player game might want Crimson to load, spawn, and end a session, and when Godot's scene startup is enough. Keep connection examples focused on making and checking the connection.
- Use a believable game situation. Say what the player does and what changes. If a guide teaches multiple ways to do something, give each way a complete example. For GDScript examples, use explicit types, named constants for meaningful values, and top-level typed constants for preloaded assets. State which scripts, controls, or assets the example needs. Check results and show the next step or failure. Mark teaching excerpts as parts of the complete script.
- Keep quickstarts small. Include screenshots when they show the expected result. Capture the actual running example or label an illustrative view.

## Maintain the API Reference

- Follow the Unity Script Reference model. A module opens an overview explaining its purpose and grouping its types. A class page explains the class and links to each method and property on its own page. Include signatures, parameters, defaults, return values, and failure behavior where applicable. For properties, show the type, access, and default when documented. Document signals, enums, and constants on their owning types. Link named APIs in guide prose to the exact reference page or section.
- Use collapsible API groups with clear breadcrumbs and visible current-page highlighting. Keep Mintlify's default styling and readable member names. Fix navigation and layout through supported configuration before reaching for custom CSS.
- Give API examples a short premise, a trigger, and an observable result. Keep editor setup to a brief prerequisite. Use an authored example that shows why the member matters in a game or tool. When a relevant example is shared, link to it. Avoid signature-derived wrappers that only call the method or assign a value.
- Update the owning class XML or documentation generator inputs and regenerate reference pages. Check the Mintlify build, internal links and anchors, and sidebar selection. Check GDScript against the matching addon and run representative examples. Report what was run, what was only syntax-checked, and what still needs a live test.
