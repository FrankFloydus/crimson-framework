<!-- Copyright Francesco Di Blasi 2026 - All Rights Reserved -->

# Gameplay Attributes proposal

Use attributes to give each character numeric gameplay state such as health, stamina, and movement speed. The game defines the attributes in Project Settings, assigns sets to the actor's Ability System Component, and sets values in authority gameplay code. Clients can read the server's confirmed values.

The first implementation is available. See the [Gameplay Attributes guide](../src/modules/attributes/README.md) for the shipped editor workflow and runtime API.

## What Unreal does

Unreal Engine 5.8 identifies a gameplay attribute by a reflected property on a `UAttributeSet`. The actor's Ability System Component registers its set instances, and gameplay code retrieves the instance by class. `FGameplayAttributeData` starts with base and current values of zero unless initialized. Epic's Action RPG example applies a Stats Gameplay Effect to initialize them. Gameplay code can enforce rules such as keeping Health within its own MaxHealth attribute. [Epic Ability System Component and Attributes](https://dev.epicgames.com/documentation/en-us/unreal-engine/gameplay-ability-system-component-and-gameplay-attributes-in-unreal-engine) · [Epic Action RPG attributes and effects](https://dev.epicgames.com/documentation/en-us/unreal-engine/attributes-and-effects-in-arpg?application_version=4.27)

Unreal replicates configured attributes as properties on the set. Its replication notification feeds the confirmed value through GAS so the client can update observers and reconcile predicted modifiers when it uses them. The installed sources show the property notification in `GameplayPrediction.h` and the client update in `GameplayEffect.cpp` under `SetBaseAttributeValueFromReplication`. [Epic Gameplay Attributes and Attribute Sets](https://dev.epicgames.com/documentation/en-us/unreal-engine/gameplay-attributes-and-attribute-sets-for-the-gameplay-ability-system-in-unreal-engine?application_version=5.8)

## Register attribute sets in Project Settings

Store one versioned registry under `crimson/gameplay_attributes/sets` in Project Settings. Add a Gameplay Attributes tab that lists the sets and lets you add attribute rows, validate the registry, save it to `project.godot`, and run Generate. Godot exposes a supported editor plugin container for custom Project Settings tabs. [Godot ProjectSettings](https://docs.godotengine.org/en/4.7/classes/class_projectsettings.html) · [Godot Project Settings tab API](https://docs.godotengine.org/en/4.7/classes/class_editorplugin.html#class-editorplugin-constant-container-project-setting-tab-right)

| Field | Example | Contract |
| --- | --- | --- |
| Set ID | `vitals` | Required and unique across the project. |
| Attribute ID | `health` | Required and unique within the set. Together the IDs form `vitals.health`. |
| Display label | Health | Optional. The editor derives a readable label from the ID when this field is empty. Games can add or localize labels later. |
| Replication | Observers | Controls who can receive a confirmed value from the authority. |

Use lowercase snake case for each ID segment. Use the qualified ID for runtime lookup, saved values, replication, and deterministic generation. Keep labels and row order out of schema compatibility checks. Treat changes to shipped IDs as explicit versioned migrations that map old IDs to new ones. If `vitals.health` gets the label Hit Points, retain its ID and generated symbols.

Validate the whole registry before saving or generating. Reject duplicate IDs and generated symbol collisions. The Project Settings definitions store identity and replication metadata. Assign sets to each actor's Ability System Component in the scene Inspector.

## Generate game C++ accessors

Generate one deterministic, game-owned header per set. Use the header's `find_set()` with the actor's Ability System Component to resolve a typed view. For `vitals.health`, it provides `get_health()`, `get_health_base()`, `set_health_base()`, `add_health_base()`, and `bind_health_changed()`. Check the full emitted symbol list for collisions such as a `health_base` entry introducing an accessor also generated for `health`.

Have the generated helpers call the public Godot bindings at the Crimson boundary. The native Attributes module owns numeric state, typed results, and change notification. The generated change helper subscribes to the bound entity's attribute and receives the old and new value after commit. GDScript and Visual Script can use the same generic runtime reads, authority edits, and change signal. The shipped Windows DLL exports the GDExtension entry point. A game-owned extension can use generated headers through Godot's binding API.

Generate writes source. Compile the game code that consumes the generated header and reopen the editor when loading a rebuilt non-reloadable extension. Editing registry data takes effect in a new session using the same Crimson addon. [Crimson extension descriptor](../project/addons/crimson-framework/crimson-framework.gdextension)

## Native values and edits

- Put the schema, validation, and numeric operations in a standalone native Attributes module. Use finite `double` values and resolve IDs by exact identity. A local game can run the same operations without creating a network session.
- Initialize each assigned attribute's base and current to zero. The game can set a starting base on the actor's set or seed a coordinated character in the authority transaction that creates it. Store the committed base and derive the current reading from it in this milestone.
- Provide typed reads that can report an unavailable value. `set_base()` and `add_base()` return a Godot `Error`. Validate finite arithmetic before committing. The game owns numeric rules such as the relationship between `vitals.health` and `vitals.max_health`. An update that ends at the existing value emits no change.
- Publish the detached readings after the whole edit succeeds. Notify observers with the attribute ID and old and new readings so a health bar can read a committed value. Later Gameplay Effects can contribute to current values through an aggregator using the same authority and notification boundary.

Keep game rules such as damage permissions and zero-health behavior in the game.

## Replicate confirmed values

Use coordinated Game Flow for multiplayer. The Attributes integration registers a match store through `TransactionCoordinator`. The store owns the authoritative entity values by stable character and attribute IDs. Bind a Match Ability System Component to its character through `MatchAttributes.bind_component()` so the actor reads that store or its recipient projection. [Match transactions and committed projections](PUBLIC_API.md#authority-gameplay-and-visibility)

1. Route gameplay requests through the existing command path. The authority uses the admitted player and current character binding to validate the action and calculate the numeric result. Commit the attribute edit through the same store in offline, listen-host, and dedicated modes.
2. Project only the confirmed current values the recipient may read. Each definition can expose the value to admitted observers of the character or to its owner. Default to authority-side visibility until a definition opts in. Use a bounded detached record per visible character with stable String keys. Compose those records with the game's existing recipient projector. Publish visibility and ownership changes through the same committed path so revoked values are removed.
3. Use `StateReplication` for each admitted peer's baseline and committed updates on its current world epoch. Validate the schema and values in a detached view before exposing them to the game. A client health bar starts from that confirmed view. Treat an absent value as unavailable, clear stale entries on removal or world changes, and use the existing recovery path when a projection cannot be applied. Attribute changes report transitions observed in the recipient's view. [State replication contract and bounds](../src/modules/network/README.md#committed-state)
4. Reserve client prediction and temporary modifier replication for the effects milestone. Checkpoint integration can persist authoritative base values by stable IDs through the match store's existing capture and restore path. Test same-schema restore and establish a migration before enabling the new store for older checkpoints, since coordinated restore expects the registered store set to match. [Checkpoint state](PUBLIC_API.md#save-travel-and-restore)

The first playable proof uses the coordinated lobby, which admits peers before the match starts. Cover resynchronization and world travel there. The FPS demo uses the fixed-world `Game.host()` path with a separate World Session lifecycle. Replicating attributes in that demo or supporting mid-match drop-in requires an explicit integration step. [Fixed-world and coordinated entry points](PUBLIC_API.md#start-a-coordinated-game)

## First playable proof

Author health and assign the set to two characters' components. Set each character's starting health to 100 in code. Join through the lobby and trigger a server-validated hit for 25. The affected character shows 75 on the authority and in a remote client view. The other character has its own value of 100. A replication fixture also seeds a baseline after a committed hit so a client's first received value can be 75.

1. Verify Project Settings schema loading, actor component access, finite game edits, independent actor values, typed readings and change notifications.
2. Build the Gameplay Attributes editor and Generate action. Verify save and reopen of the registry and check that label or row-order changes preserve generated API names. Compile generated code from a separate game extension using the distributed addon. Exercise generic reads in GDScript and Visual Script.
3. Implement the coordinated authority and recipient projection adapter. Verify a listen host and a separate-process playerless authority, lobby admission and baseline, authenticated client requests, travel and resynchronization, owner-only visibility, removal, and schema rejection. Assert that rejected writes leave the committed revision and values intact. Run Windows debug and release fixtures with Godot 4.7.
