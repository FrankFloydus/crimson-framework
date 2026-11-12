// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#include "integration/game/game_services.hpp"

#include "integration/game/crimson_game.hpp"

#include <godot_cpp/classes/os.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/core/memory.hpp>

#include <map>

namespace crimson::integration {
namespace {

// Scoped service selection and visibility while the owning game boots.
struct GameScope {
    // Provider captured before creating the game-owned service nodes.
    godot::Ref<GameServiceProvider> provider;
    // Handle invalidated when this particular game scope exits.
    godot::Ref<SceneService> scene_service;
    // Exact online binding for this attachment, including a startup diagnostic.
    godot::Ref<OnlineDirectory> online_directory;
    // Set after the game has attached its services and wired their dependencies.
    bool published = false;
};

// Game IDs resolve weakly and never keep a scene node alive.
std::map<uint64_t, GameScope> scopes;
// Explicit primary game when a process runs more than one scope.
godot::ObjectID primary_game;

// Godot scene nodes and service bindings are owned by the main thread.
bool on_main_thread() {
    const godot::OS *os = godot::OS::get_singleton();
    return os && os->get_thread_caller_id() == os->get_main_thread_id();
}

// Resolve a scope's owner without extending its lifetime.
GameRuntime *live_game(godot::ObjectID id) {
    const auto scope = scopes.find(uint64_t(id));
    if (scope == scopes.end() || !scope->second.published) {
        return nullptr;
    }
    auto *game = godot::Object::cast_to<GameRuntime>(godot::ObjectDB::get_instance(id));
    return game && game->is_inside_tree() && !game->is_queued_for_deletion() ? game : nullptr;
}

// Check the detached-node contract before transferring ownership to GameRuntime.
bool detached(const godot::Node *node) {
    return node && !node->get_parent() && !node->is_queued_for_deletion();
}

} // namespace

using namespace godot;

void GameServiceProvider::_bind_methods() {
    GDVIRTUAL_BIND(_create_session);
    GDVIRTUAL_BIND(_create_scene_loader);
    GDVIRTUAL_BIND(_create_online_directory);
}

session::GameSession *GameServiceProvider::create_session() {
    if (!GDVIRTUAL_IS_OVERRIDDEN(_create_session)) {
        return memnew(session::GameSession);
    }
    session::GameSession *service = nullptr;
    GDVIRTUAL_CALL(_create_session, service);
    return service;
}

Ref<OnlineDirectory> GameServiceProvider::create_online_directory() {
    Ref<OnlineDirectory> directory;
    GDVIRTUAL_CALL(_create_online_directory, directory);
    return directory;
}

session::GameSession *GameServiceProvider::create_session_for(GameRuntime &) {
    return create_session();
}

Ref<OnlineDirectory> GameServiceProvider::create_online_directory_for(GameRuntime &, session::GameSession &) {
    return create_online_directory();
}

void SceneService::_bind_methods() {
    ClassDB::bind_method(D_METHOD("load", "world_id"), &SceneService::load);
    ClassDB::bind_method(D_METHOD("get_world"), &SceneService::get_world);
}

GameRuntime *SceneService::resolve_game() const {
    if (!on_main_thread()) {
        return nullptr;
    }
    const auto scope = scopes.find(uint64_t(game_id));
    return scope != scopes.end() && scope->second.scene_service.ptr() == this ? live_game(game_id) : nullptr;
}

Ref<GameOperation> SceneService::load(const StringName &world_id) {
    if (GameRuntime *game = resolve_game()) {
        return game->travel_to(world_id);
    }
    Dictionary result;
    result["ok"] = false;
    result["code"] = "Unavailable";
    result["message"] = "The originating GameRuntime scene service is no longer active.";
    result["world_id"] = String();
    result["world_epoch"] = int64_t(0);
    Ref<GameOperation> operation;
    operation.instantiate();
    operation->finish(result);
    return operation;
}

Node *SceneService::get_world() const {
    const GameRuntime *game = resolve_game();
    return game ? game->get_world() : nullptr;
}

void GameServiceContainer::open(GameRuntime &game) {
    const Ref<GameDefinition> definition = game.get_definition();
    GameScope scope;
    if (definition.is_valid()) {
        scope.provider = definition->get_service_provider();
    }
    scope.scene_service.instantiate();
    scope.scene_service->game_id = ObjectID(game.get_instance_id());
    scopes.emplace(game.get_instance_id(), scope);
}

void GameServiceContainer::publish(GameRuntime &game) {
    const auto scope = scopes.find(game.get_instance_id());
    if (scope != scopes.end()) {
        scope->second.published = true;
    }
}

void GameServiceContainer::close(GameRuntime &game) {
    auto retired = scopes.extract(game.get_instance_id());
    if (primary_game == ObjectID(game.get_instance_id())) {
        primary_game = ObjectID();
    }
    if (!retired.empty() && retired.mapped().online_directory.is_valid()) {
        retired.mapped().online_directory->retire_attachment();
    }
}

session::GameSession *GameServiceContainer::make_session(GameRuntime &game) {
    const auto scope = scopes.find(game.get_instance_id());
    if (scope == scopes.end()) {
        return nullptr;
    }
    const Ref<GameServiceProvider> provider = scope->second.provider;
    if (provider.is_null()) {
        return memnew(session::GameSession);
    }
    session::GameSession *service = provider->create_session_for(game);
    return detached(service) ? service : nullptr;
}

scene_load::SceneLoadManager *GameServiceContainer::make_scene_loader(GameRuntime &game) {
    const auto scope = scopes.find(game.get_instance_id());
    if (scope == scopes.end()) {
        return nullptr;
    }
    const Ref<GameServiceProvider> provider = scope->second.provider;
    if (provider.is_null() || !GDVIRTUAL_IS_OVERRIDDEN_PTR(provider.ptr(), _create_scene_loader)) {
        return memnew(scene_load::SceneLoadManager);
    }
    scene_load::SceneLoadManager *service = nullptr;
    GDVIRTUAL_CALL_PTR(provider.ptr(), _create_scene_loader, service);
    return detached(service) ? service : nullptr;
}

Ref<OnlineDirectory> GameServiceContainer::make_online_directory(GameRuntime &game, session::GameSession &session) {
    const auto scope = scopes.find(game.get_instance_id());
    if (scope == scopes.end() || scope->second.provider.is_null()) {
        return {};
    }
    const Ref<GameServiceProvider> provider = scope->second.provider;
    const uint64_t game_id = game.get_instance_id();
    const Ref<SceneService> attachment = scope->second.scene_service;
    const Ref<OnlineDirectory> directory = provider->create_online_directory_for(game, session);
    const auto current = scopes.find(game_id);
    if (current == scopes.end() || current->second.scene_service != attachment) {
        return {};
    }
    current->second.online_directory = directory;
    return directory.is_valid() && directory->is_game_available() ? directory : Ref<OnlineDirectory>();
}

Ref<OnlineDirectory> GameServiceContainer::online_for(const GameRuntime *game) {
    if (!on_main_thread() || !game) {
        return {};
    }
    const ObjectID id(game->get_instance_id());
    return live_game(id) == game ? scopes.at(uint64_t(id)).online_directory : Ref<OnlineDirectory>();
}

Ref<OnlineDirectory> GameServiceContainer::unique_online_for(const GameServiceProvider &provider, bool &ambiguous) {
    ambiguous = false;
    if (!on_main_thread()) {
        return {};
    }
    const GameScope *found = nullptr;
    for (const auto &[id, scope] : scopes) {
        if (scope.provider.ptr() != &provider || !live_game(ObjectID(id))) {
            continue;
        }
        if (found) {
            ambiguous = true;
            return {};
        }
        found = &scope;
    }
    return found ? found->online_directory : Ref<OnlineDirectory>();
}

GameRuntime *GameServiceContainer::default_game() {
    if (!on_main_thread()) {
        return nullptr;
    }
    if (GameRuntime *selected = live_game(primary_game)) {
        return selected;
    }
    GameRuntime *only = nullptr;
    for (const auto &scope : scopes) {
        if (GameRuntime *game = live_game(ObjectID(scope.first))) {
            if (only) {
                return nullptr;
            }
            only = game;
        }
    }
    return only;
}

Ref<SceneService> GameServiceContainer::default_scenes() {
    GameRuntime *game = default_game();
    if (!game) {
        return {};
    }
    return scopes.at(game->get_instance_id()).scene_service;
}

Error GameServiceContainer::select_default(GameRuntime *game) {
    if (!on_main_thread()) {
        return ERR_UNAVAILABLE;
    }
    const ObjectID id = game ? ObjectID(game->get_instance_id()) : ObjectID();
    if (game && live_game(id) != game) {
        return ERR_INVALID_PARAMETER;
    }
    primary_game = id;
    return OK;
}

GameRuntime *GameServiceContainer::game_for(Node *node) {
    if (!on_main_thread()) {
        return nullptr;
    }
    for (Node *ancestor = node; ancestor; ancestor = ancestor->get_parent()) {
        if (GameRuntime *game = live_game(ObjectID(ancestor->get_instance_id()))) {
            return game;
        }
    }
    return nullptr;
}

void GameServices::_bind_methods() {
    ClassDB::bind_static_method(get_class_static(), D_METHOD("game"), &GameServices::game);
    ClassDB::bind_static_method(get_class_static(), D_METHOD("scenes"), &GameServices::scenes);
    ClassDB::bind_static_method(get_class_static(), D_METHOD("for_node", "node"), &GameServices::for_node);
    ClassDB::bind_static_method(get_class_static(), D_METHOD("set_default_game", "game"), &GameServices::set_default_game);
}

GameRuntime *GameServices::game() {
    return GameServiceContainer::default_game();
}

Ref<SceneService> GameServices::scenes() {
    return GameServiceContainer::default_scenes();
}

GameRuntime *GameServices::for_node(Node *node) {
    return GameServiceContainer::game_for(node);
}

Error GameServices::set_default_game(GameRuntime *game) {
    return GameServiceContainer::select_default(game);
}

} // namespace crimson::integration
