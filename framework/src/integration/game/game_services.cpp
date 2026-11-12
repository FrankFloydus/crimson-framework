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
    godot::Ref<CrimsonServiceProvider> provider;
    // Handle invalidated when this particular game scope exits.
    godot::Ref<CrimsonSceneService> scene_service;
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
CrimsonGame *live_game(godot::ObjectID id) {
    const auto scope = scopes.find(uint64_t(id));
    if (scope == scopes.end() || !scope->second.published) {
        return nullptr;
    }
    auto *game = godot::Object::cast_to<CrimsonGame>(godot::ObjectDB::get_instance(id));
    return game && game->is_inside_tree() && !game->is_queued_for_deletion() ? game : nullptr;
}

// Check the detached-node contract before transferring ownership to CrimsonGame.
bool detached(const godot::Node *node) {
    return node && !node->get_parent() && !node->is_queued_for_deletion();
}

} // namespace

using namespace godot;

void CrimsonServiceProvider::_bind_methods() {
    GDVIRTUAL_BIND(_create_session);
    GDVIRTUAL_BIND(_create_scene_loader);
    GDVIRTUAL_BIND(_create_online_directory);
}

session::CrimsonGameSession *CrimsonServiceProvider::create_session() {
    if (!GDVIRTUAL_IS_OVERRIDDEN(_create_session)) {
        return memnew(session::CrimsonGameSession);
    }
    session::CrimsonGameSession *service = nullptr;
    GDVIRTUAL_CALL(_create_session, service);
    return service;
}

Ref<CrimsonOnlineDirectory> CrimsonServiceProvider::create_online_directory() {
    Ref<CrimsonOnlineDirectory> directory;
    GDVIRTUAL_CALL(_create_online_directory, directory);
    return directory;
}

void CrimsonSceneService::_bind_methods() {
    ClassDB::bind_method(D_METHOD("load", "world_id"), &CrimsonSceneService::load);
    ClassDB::bind_method(D_METHOD("get_world"), &CrimsonSceneService::get_world);
}

CrimsonGame *CrimsonSceneService::resolve_game() const {
    if (!on_main_thread()) {
        return nullptr;
    }
    const auto scope = scopes.find(uint64_t(game_id));
    return scope != scopes.end() && scope->second.scene_service.ptr() == this ? live_game(game_id) : nullptr;
}

Ref<CrimsonGameOperation> CrimsonSceneService::load(const StringName &world_id) {
    if (CrimsonGame *game = resolve_game()) {
        return game->travel_to(world_id);
    }
    Dictionary result;
    result["ok"] = false;
    result["code"] = "Unavailable";
    result["message"] = "The originating CrimsonGame scene service is no longer active.";
    result["world_id"] = String();
    result["world_epoch"] = int64_t(0);
    Ref<CrimsonGameOperation> operation;
    operation.instantiate();
    operation->finish(result);
    return operation;
}

Node *CrimsonSceneService::get_world() const {
    const CrimsonGame *game = resolve_game();
    return game ? game->get_world() : nullptr;
}

void CrimsonServiceContainer::open(CrimsonGame &game) {
    const Ref<CrimsonGameDefinition> definition = game.get_definition();
    GameScope scope;
    if (definition.is_valid()) {
        scope.provider = definition->get_service_provider();
    }
    scope.scene_service.instantiate();
    scope.scene_service->game_id = ObjectID(game.get_instance_id());
    scopes.emplace(game.get_instance_id(), scope);
}

void CrimsonServiceContainer::publish(CrimsonGame &game) {
    const auto scope = scopes.find(game.get_instance_id());
    if (scope != scopes.end()) {
        scope->second.published = true;
    }
}

void CrimsonServiceContainer::close(CrimsonGame &game) {
    scopes.erase(game.get_instance_id());
    if (primary_game == ObjectID(game.get_instance_id())) {
        primary_game = ObjectID();
    }
}

session::CrimsonGameSession *CrimsonServiceContainer::make_session(CrimsonGame &game) {
    const auto scope = scopes.find(game.get_instance_id());
    if (scope == scopes.end()) {
        return nullptr;
    }
    const Ref<CrimsonServiceProvider> provider = scope->second.provider;
    if (provider.is_null()) {
        return memnew(session::CrimsonGameSession);
    }
    session::CrimsonGameSession *service = provider->create_session();
    return detached(service) ? service : nullptr;
}

scene_load::CrimsonSceneLoadManager *CrimsonServiceContainer::make_scene_loader(CrimsonGame &game) {
    const auto scope = scopes.find(game.get_instance_id());
    if (scope == scopes.end()) {
        return nullptr;
    }
    const Ref<CrimsonServiceProvider> provider = scope->second.provider;
    if (provider.is_null() || !GDVIRTUAL_IS_OVERRIDDEN_PTR(provider.ptr(), _create_scene_loader)) {
        return memnew(scene_load::CrimsonSceneLoadManager);
    }
    scene_load::CrimsonSceneLoadManager *service = nullptr;
    GDVIRTUAL_CALL_PTR(provider.ptr(), _create_scene_loader, service);
    return detached(service) ? service : nullptr;
}

Ref<CrimsonOnlineDirectory> CrimsonServiceContainer::make_online_directory(CrimsonGame &game) {
    const auto scope = scopes.find(game.get_instance_id());
    if (scope == scopes.end() || scope->second.provider.is_null()) {
        return {};
    }
    const Ref<CrimsonServiceProvider> provider = scope->second.provider;
    return provider->create_online_directory();
}

CrimsonGame *CrimsonServiceContainer::default_game() {
    if (!on_main_thread()) {
        return nullptr;
    }
    if (CrimsonGame *selected = live_game(primary_game)) {
        return selected;
    }
    CrimsonGame *only = nullptr;
    for (const auto &scope : scopes) {
        if (CrimsonGame *game = live_game(ObjectID(scope.first))) {
            if (only) {
                return nullptr;
            }
            only = game;
        }
    }
    return only;
}

Ref<CrimsonSceneService> CrimsonServiceContainer::default_scenes() {
    CrimsonGame *game = default_game();
    if (!game) {
        return {};
    }
    return scopes.at(game->get_instance_id()).scene_service;
}

Error CrimsonServiceContainer::select_default(CrimsonGame *game) {
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

CrimsonGame *CrimsonServiceContainer::game_for(Node *node) {
    if (!on_main_thread()) {
        return nullptr;
    }
    for (Node *ancestor = node; ancestor; ancestor = ancestor->get_parent()) {
        if (CrimsonGame *game = live_game(ObjectID(ancestor->get_instance_id()))) {
            return game;
        }
    }
    return nullptr;
}

void Crimson::_bind_methods() {
    ClassDB::bind_static_method(get_class_static(), D_METHOD("game"), &Crimson::game);
    ClassDB::bind_static_method(get_class_static(), D_METHOD("scenes"), &Crimson::scenes);
    ClassDB::bind_static_method(get_class_static(), D_METHOD("for_node", "node"), &Crimson::for_node);
    ClassDB::bind_static_method(get_class_static(), D_METHOD("set_default_game", "game"), &Crimson::set_default_game);
}

CrimsonGame *Crimson::game() {
    return CrimsonServiceContainer::default_game();
}

Ref<CrimsonSceneService> Crimson::scenes() {
    return CrimsonServiceContainer::default_scenes();
}

CrimsonGame *Crimson::for_node(Node *node) {
    return CrimsonServiceContainer::game_for(node);
}

Error Crimson::set_default_game(CrimsonGame *game) {
    return CrimsonServiceContainer::select_default(game);
}

} // namespace crimson::integration
