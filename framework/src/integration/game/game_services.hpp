// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#pragma once

#include "integration/game/game_operation.hpp"
#include "integration/online/online_directory.hpp"
#include "modules/scene_load/scene_load_manager.hpp"
#include "modules/session/game_session.hpp"

#include <godot_cpp/classes/object.hpp>
#include <godot_cpp/classes/resource.hpp>
#include <godot_cpp/core/binder_common.hpp>
#include <godot_cpp/core/gdvirtual.gen.inc>

namespace crimson::integration {

class GameRuntime;

// Optional factories for the native services owned by one GameRuntime.
class GameServiceProvider : public godot::Resource {
    GDCLASS(GameServiceProvider, godot::Resource)

    friend class GameServiceContainer;

protected:
    // Supply a native detached session or delegate to the scripted factory by default.
    virtual session::GameSession *create_session();
    // Supply a native directory or delegate to the scripted factory by default.
    virtual godot::Ref<OnlineDirectory> create_online_directory();
    // Create a Session for the exact Game without changing the script factory signature.
    virtual session::GameSession *create_session_for(GameRuntime &game);
    // Create a binding for this Game's actual persistent Session.
    virtual godot::Ref<OnlineDirectory> create_online_directory_for(GameRuntime &game,
            session::GameSession &session);
    // Build an unattached session node, or use the native default when this hook is absent.
    GDVIRTUAL0R(session::GameSession *, _create_session)
    // Build an unattached scene loader, or use the native default when this hook is absent.
    GDVIRTUAL0R(scene_load::SceneLoadManager *, _create_scene_loader)
    // Create the optional game-owned online directory for this scope.
    GDVIRTUAL0R(godot::Ref<OnlineDirectory>, _create_online_directory)
    // Register the provider's typed override hooks.
    static void _bind_methods();
};

// Authority-aware scene actions for the game scope that created this handle.
class SceneService : public godot::RefCounted {
    GDCLASS(SceneService, godot::RefCounted)

    friend class GameServiceContainer;

    // Weak reference validated against the originating scope on each call.
    godot::ObjectID game_id;
    // Resolve the game only while it owns this particular service instance.
    GameRuntime *resolve_game() const;

protected:
    // Register guarded travel and mounted-world access.
    static void _bind_methods();

public:
    // Travel through the owning game's coordinated Scene Load lifecycle.
    godot::Ref<GameOperation> load(const godot::StringName &world_id);
    // Return the currently mounted world while the scope remains active.
    godot::Node *get_world() const;
};

// Holds game-scoped bindings and weak lookups for the Godot-facing facade.
class GameServiceContainer {
public:
    // Capture the game's selected provider before constructing its services.
    static void open(GameRuntime &game);
    // Make the fully wired game available to global and contextual lookups.
    static void publish(GameRuntime &game);
    // Release the game's bindings and any explicit default designation.
    static void close(GameRuntime &game);
    // Construct the game's detached session for injection at boot.
    static session::GameSession *make_session(GameRuntime &game);
    // Construct the game's detached loader for injection at boot.
    static scene_load::SceneLoadManager *make_scene_loader(GameRuntime &game);
    // Resolve a game-supplied directory without altering direct session defaults.
    static godot::Ref<OnlineDirectory> make_online_directory(GameRuntime &game, session::GameSession &session);
    // Resolve the selected native directory binding for a published live game.
    static godot::Ref<OnlineDirectory> online_for(const GameRuntime *game);
    // Resolve a legacy provider call only when it identifies one published Game.
    static godot::Ref<OnlineDirectory> unique_online_for(const GameServiceProvider &provider, bool &ambiguous);
    // Return the explicitly selected game, or the sole active game.
    static GameRuntime *default_game();
    // Resolve the scene service belonging to the current primary game.
    static godot::Ref<SceneService> default_scenes();
    // Select an active game as the global default, or clear the selection with null.
    static godot::Error select_default(GameRuntime *game);
    // Resolve an active game from a node inside that game's subtree.
    static GameRuntime *game_for(godot::Node *node);
};

// Typed global access to Crimson's active game scopes.
class GameServices : public godot::Object {
    GDCLASS(GameServices, godot::Object)

protected:
    // Register the static Godot facade methods.
    static void _bind_methods();

public:
    // Return the explicitly selected game, or the sole active game.
    static GameRuntime *game();
    // Resolve the current primary game's authority-aware scene service.
    static godot::Ref<SceneService> scenes();
    // Resolve an active game from the caller's subtree.
    static GameRuntime *for_node(godot::Node *node);
    // Select a registered game as the global default, or clear the selection with null.
    static godot::Error set_default_game(GameRuntime *game);
};

} // namespace crimson::integration
