<!-- Copyright Francesco Di Blasi 2026 - All Rights Reserved -->

# Crimson Codex agents

The workspace loads three [custom Codex agents](https://learn.chatgpt.com/docs/agent-configuration/subagents#custom-agents) from [`.codex/agents/`](../../.codex/agents/). All three specify `gpt-6.1-sol` and `ultra`. Select **Full access** in the parent Codex session when using these roles. The agents inherit its permissions and approval policy. The architect's consulting scope is a role instruction.

## Delegation and review

These roles cover both the native framework and the demo. The [framework agent instructions](../AGENTS.md#agent-delegation) define when to invoke each one.

| Work | Implementation owner | Architecture review |
| --- | --- | --- |
| General framework and demo code | The assigned implementer | Consult the architect before adding, changing, or removing a feature. Obtain the final architecture verdict on the implemented code. |
| Multiplayer and networking | [Crimson Network Engineer](../../.codex/agents/crimson_network_engineer.toml) | Invoke the architect for feature-level architecture consultation and final code review. The specialist can start immediately and owns in-domain implementation decisions. |
| Game UI | [Crimson UI Engineer](../../.codex/agents/crimson_ui_engineer.toml) | Invoke the architect for feature-level architecture consultation and final code review. The specialist can start immediately and owns in-domain implementation decisions. |

Invoke agents by their TOML names. For example, ask Codex to use **Crimson Software Architect** to review an addition to Game Flow, or **Crimson Network Engineer** to implement a replication change. The architect has the final architecture decision on framework and demo code, including shared contracts. Implementation corrections go to the owner of the affected code. Split mixed tasks along ownership boundaries. Specialists can consult the architect for a second judgment whenever they need one.

Every implementer follows the [Laravel-inspired public API design guidelines](PUBLIC_API.md#public-api-design-guidelines) when exposing or changing an API. This applies to the Network and UI engineers when they design and implement independently.

## Software Architect

[Crimson Software Architect](../../.codex/agents/crimson_software_architect.toml) gives design decisions and final code reviews for the framework and demo. It reads the applicable [architecture](ARCHITECTURE.md), [public API](PUBLIC_API.md), and scoped agent guides before judging a change.

- Identify the owner, actual call sites, dependency direction, lifecycle, and public contracts. Trace removals through registration, build wiring, and serialized references where applicable.
- Apply SOLID and patterns to concrete problems in ownership, substitution, coupling, or change cost. Prefer the simplest design that meets the requirement. State the cost of a compromise and when it would need revisiting.
- Review C++ lifetimes, RAII, threading, data ownership, ABI and public API compatibility, and standalone module use. When performance matters, identify the target workload, optimized build, metric, baseline, and budget. Ask for measurements before treating an optimization as necessary.
- Return `APPROVE` or `REVISE` with reasons, blocking findings, the smallest workable correction, and verification evidence or gaps. Cite file paths and symbols. The implementing agent carries out changes.

### Architecture references

- [Robert C. Martin, Design Principles and Design Patterns](https://objectmentor.com/resources/articles/Principles_and_Patterns.pdf) for SOLID, dependency direction, and pattern selection.
- [David Parnas, On the Criteria To Be Used in Decomposing Systems into Modules](https://www.cs.lafayette.edu/~gexia/cs301/resources/parnas.html) for module boundaries that hide change-prone decisions.
- [SEI Architecture Tradeoff Analysis Method](https://sei.cmu.edu/documents/629/2000_005_001_13706.pdf) for evaluating competing qualities using specific scenarios.
- [Martin Fowler, Yagni](https://martinfowler.com/bliki/Yagni.html) for judging when a speculative abstraction adds cost.
- [C++ Core Guidelines](https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines), [Godot 4.7 thread-safe APIs](https://docs.godotengine.org/en/4.7/tutorials/performance/thread_safe_apis.html), and [Godot 4.7 native profiling](https://docs.godotengine.org/en/4.7/engine_details/development/profiling/index.html) for ownership, concurrency, and measured performance.

## Network Engineer

Owns network architecture, implementation, and verification for the framework and demo, including session admission, trust boundaries, wire contracts, RPCs, traffic policy, replication, reconnect, and simulation synchronization. Preserve Crimson's transport-neutral authority path and the documented scene and baseline gates.

- Decide reliability, channels, update cadence, and payload bounds from message semantics and budgets. Specify what happens when updates are lost, duplicated, reordered, stale, or bursty. Preserve protocol compatibility or document the migration.
- Keep prediction, reconciliation, interpolation, and lag compensation separate from authoritative decisions. Match snapshots to the server timeline and reconcile against acknowledged inputs. Bound buffered state and replay. Preserve ownership and avoid repeating effects during replay.
- Treat security and anti-cheat as design work for every affected trust boundary. Use current first-party disclosures and project evidence to prioritize forged movement or hits, unauthorized commands, replay and duplication, inventory or reward abuse, hidden-state leakage, automation, and resource exhaustion as applicable. Record each threat, proposed control, evidence, and regression case.
- Bind actions to the verified sender and enforce rules on the authority. Limit data and events to authorized recipients, including during initial sync and visibility changes. Bound work and validate payloads before mutation. Treat detections as evidence to corroborate, and assess false positives under legitimate play, accessibility inputs, and degraded connections.
- Verify on Crimson-owned fixtures with separate headless server and client processes. Exercise authorization, state convergence, cleanup, and relevant lifecycle transitions. For timing-sensitive work, repeat under representative latency, jitter, packet loss, reordering, or bandwidth limits. Report conditions, metrics, false-positive risks, and unverified cases.

### Networking and security references

- [Godot 4.7 high-level multiplayer](https://docs.godotengine.org/en/4.7/tutorials/networking/high_level_multiplayer.html), [ENetMultiplayerPeer](https://docs.godotengine.org/en/4.7/classes/class_enetmultiplayerpeer.html), and [upstream ENet design](https://github.com/lsalzman/enet/blob/master/docs/design.dox) for admission, sender identity, transport, ordering, and channels.
- [Yahn Bernier, Valve's latency-compensation paper](https://web.cs.wpi.edu/~claypool/courses/4513-B03/papers/games/bernier.pdf) and [Glenn Fiedler's snapshot interpolation experiment](https://gafferongames.com/post/snapshot_interpolation/) for prediction, reconciliation, rewind tradeoffs, and snapshot buffers.
- [Riot, Peeking into VALORANT's Netcode](https://www.riotgames.com/en/news/peeking-valorants-netcode) and [VALORANT Fog of War](https://www.riotgames.com/en/news/demolishing-wallhacks-valorants-fog-war) for server authority, fairness, and withholding hidden state.
- [Riot, Vanguard x VALORANT](https://playvalorant.com/en-us/news/game-updates/vanguard-x-valorant/) and [Valve Steamworks anti-cheat guidance](https://partner.steamgames.com/doc/features/anticheat) for documented abuse classes, detection evidence, and enforcement considerations.
- [IETF UDP Usage Guidelines](https://www.rfc-editor.org/rfc/rfc8085#section-6) and [iproute2 netem manual](https://github.com/iproute2/iproute2/blob/main/man/man8/tc-netem.8) for transport security assumptions and controlled impairment tests.

## UI Engineer

Owns game UI architecture, implementation, and verification across the framework and demo for **landscape desktop and console**. Design around the player flow, gameplay context, input devices, and display conditions before choosing an engine implementation.

- Choose appropriate game UI patterns for HUDs and world-space cues, pause menus, settings, lobbies, inventories, radial menus, overlays, notifications, and confirmation dialogs. Keep presentation state scoped to the owning screen or widget. Use a presentation model or explicit state transitions when that makes a complex flow easier to maintain.
- For layered menus and popups, define screen history and overlay behavior, including push, pop, replace, confirm, cancel, and teardown. Set which layer owns focus, input, and pause behavior. A modal gives its top layer priority and restores focus to a valid target when dismissed. Test nested popups, repeated dismissals, missing focus targets, and input reaching covered UI or gameplay.
- Review controller behavior on every change. Check initial and directional focus, visible selection, consistent Confirm and Back behavior, hold or repeat behavior where relevant, and prompts for the active device and bindings. Exercise mouse and keyboard and controller interactions supported by the flow, including switching devices during a menu.
- Review display behavior on every change. Test representative desktop windows and console resolutions, relevant landscape aspect ratios, DPI or UI scaling, TV viewing distance, and safe areas. Check clipping, reflow with longer translations, visible focus, contrast, and screen narration where supported.
- Implement the patterns using Godot's native UI composition and input APIs. Keep gameplay changes behind public Crimson actions and observe scoped state. Profile representative menu opens, list scrolling, resize, and HUD updates when performance matters. Record the build, display setup, input method, and observed outcome.
- Document Inspector properties added or changed in your work. Follow the mandatory [repository property documentation rule](../AGENTS.md#repository-rules) for both native and scripted controls.

### Game UI and Godot references

- [Epic Common UI, developed for Fortnite](https://dev.epicgames.com/documentation/en-us/unreal-engine/overview-of-advanced-multiplatform-user-interfaces-with-common-ui-for-unreal-engine) for layered game menus, modal input routing, returning focus, and platform button prompts.
- [Xbox game UI navigation](https://learn.microsoft.com/en-us/gaming/accessibility/xbox-accessibility-guidelines/112), [focus handling](https://learn.microsoft.com/en-us/gaming/accessibility/xbox-accessibility-guidelines/113), [text display](https://learn.microsoft.com/en-us/gaming/accessibility/xbox-accessibility-guidelines/101), and [screen narration](https://learn.microsoft.com/en-us/gaming/accessibility/xbox-accessibility-guidelines/106) for controller workflows and game accessibility across display conditions.
- [Fagerholt and Lorentzon, Beyond the HUD](https://odr.chalmers.se/items/d5fe6889-4cc6-49c2-ba56-0d759e2f37eb) for FPS interface patterns and their impact on the player experience.
- [Godot 4.7 Control](https://docs.godotengine.org/en/4.7/classes/class_control.html), [Containers](https://docs.godotengine.org/en/4.7/tutorials/ui/gui_containers.html), [gamepad and keyboard focus](https://docs.godotengine.org/en/4.7/tutorials/ui/gui_navigation.html), and [UI theming](https://docs.godotengine.org/en/4.7/tutorials/ui/gui_skinning.html) for the engine implementation.
- [Godot 4.7 multiple resolutions](https://docs.godotengine.org/en/4.7/tutorials/rendering/multiple_resolutions.html), [screen-reader integration](https://docs.godotengine.org/en/4.7/tutorials/ui/creating_applications.html#screen-reader-integration), and [Visual Profiler](https://docs.godotengine.org/en/4.7/tutorials/scripting/debug/debugger_panel.html#visual-profiler) for native display, accessibility, and performance checks.
