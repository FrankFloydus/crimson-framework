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

class CrimsonGame;

// Optional factories for the native services owned by one CrimsonGame.
class CrimsonServiceProvider : public godot::Resource {
    GDCLASS(CrimsonServiceProvider, godot::Resource)

    friend class CrimsonServiceContainer;

protected:
    // Supply a native detached session or delegate to the scripted factory by default.
    virtual session::CrimsonGameSession *create_session();
    // Supply a native directory or delegate to the scripted factory by default.
    virtual godot::Ref<CrimsonOnlineDirectory> create_online_directory();
    // Build an unattached session node, or use the native default when this hook is absent.
    GDVIRTUAL0R(session::CrimsonGameSession *, _create_session)
    // Build an unattached scene loader, or use the native default when this hook is absent.
    GDVIRTUAL0R(scene_load::CrimsonSceneLoadManager *, _create_scene_loader)
    // Create the optional game-owned online directory for this scope.
    GDVIRTUAL0R(godot::Ref<CrimsonOnlineDirectory>, _create_online_directory)
    // Register the provider's typed override hooks.
    static void _bind_methods();
};

// Authority-aware scene actions for the game scope that created this handle.
class CrimsonSceneService : public godot::RefCounted {
    GDCLASS(CrimsonSceneService, godot::RefCounted)

    friend class CrimsonServiceContainer;

    // Weak reference validated against the originating scope on each call.
    godot::ObjectID game_id;
    // Resolve the game only while it owns this particular service instance.
    CrimsonGame *resolve_game() const;

protected:
    // Register guarded travel and mounted-world access.
    static void _bind_methods();

public:
    // Travel through the owning game's coordinated Scene Load lifecycle.
    godot::Ref<CrimsonGameOperation> load(const godot::StringName &world_id);
    // Return the currently mounted world while the scope remains active.
    godot::Node *get_world() const;
};

// Holds game-scoped bindings and weak lookups for the Godot-facing facade.
class CrimsonServiceContainer {
public:
    // Capture the game's selected provider before constructing its services.
    static void open(CrimsonGame &game);
    // Make the fully wired game available to global and contextual lookups.
    static void publish(CrimsonGame &game);
    // Release the game's bindings and any explicit default designation.
    static void close(CrimsonGame &game);
    // Construct the game's detached session for injection at boot.
    static session::CrimsonGameSession *make_session(CrimsonGame &game);
    // Construct the game's detached loader for injection at boot.
    static scene_load::CrimsonSceneLoadManager *make_scene_loader(CrimsonGame &game);
    // Resolve a game-supplied directory without altering direct session defaults.
    static godot::Ref<CrimsonOnlineDirectory> make_online_directory(CrimsonGame &game);
    // Return the explicitly selected game, or the sole active game.
    static CrimsonGame *default_game();
    // Resolve the scene service belonging to the current primary game.
    static godot::Ref<CrimsonSceneService> default_scenes();
    // Select an active game as the global default, or clear the selection with null.
    static godot::Error select_default(CrimsonGame *game);
    // Resolve an active game from a node inside that game's subtree.
    static CrimsonGame *game_for(godot::Node *node);
};

// Typed global access to Crimson's active game scopes.
class Crimson : public godot::Object {
    GDCLASS(Crimson, godot::Object)

protected:
    // Register the static Godot facade methods.
    static void _bind_methods();

public:
    // Return the explicitly selected game, or the sole active game.
    static CrimsonGame *game();
    // Resolve the current primary game's authority-aware scene service.
    static godot::Ref<CrimsonSceneService> scenes();
    // Resolve an active game from the caller's subtree.
    static CrimsonGame *for_node(godot::Node *node);
    // Select a registered game as the global default, or clear the selection with null.
    static godot::Error set_default_game(CrimsonGame *game);
};

} // namespace crimson::integration
