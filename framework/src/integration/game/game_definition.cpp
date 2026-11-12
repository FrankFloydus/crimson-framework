// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#include "integration/game/game_definition.hpp"

#include "integration/game/crimson_game.hpp"

#include <godot_cpp/classes/os.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/core/memory.hpp>

namespace crimson::integration {

using namespace godot;

void GameDefinition::_bind_methods() {
    ClassDB::bind_static_method(get_class_static(), D_METHOD("configure"), &GameDefinition::configure);
    ClassDB::bind_method(D_METHOD("with_worlds", "worlds"), &GameDefinition::with_worlds);
    ClassDB::bind_method(D_METHOD("starting_in", "world_id"), &GameDefinition::starting_in);
    ClassDB::bind_method(D_METHOD("with_saves", "app_id", "root", "schema_version"), &GameDefinition::with_saves,
            DEFVAL(String("user://crimson")), DEFVAL(1));
    ClassDB::bind_method(D_METHOD("with_starting_world", "world_id", "scene"), &GameDefinition::with_starting_world);
    ClassDB::bind_method(D_METHOD("with_player", "scene"), &GameDefinition::with_player);
    ClassDB::bind_method(D_METHOD("with_multiplayer", "rules"), &GameDefinition::with_multiplayer);
    ClassDB::bind_method(D_METHOD("with_provider", "provider"), &GameDefinition::with_provider);
    ClassDB::bind_method(D_METHOD("create"), &GameDefinition::create);
    ClassDB::bind_method(D_METHOD("boot", "owner"), &GameDefinition::boot);

    ClassDB::bind_method(D_METHOD("set_app_id", "app_id"), &GameDefinition::set_app_id);
    ClassDB::bind_method(D_METHOD("get_app_id"), &GameDefinition::get_app_id);
    ClassDB::bind_method(D_METHOD("set_save_root", "save_root"), &GameDefinition::set_save_root);
    ClassDB::bind_method(D_METHOD("get_save_root"), &GameDefinition::get_save_root);
    ClassDB::bind_method(D_METHOD("set_schema_version", "schema_version"), &GameDefinition::set_schema_version);
    ClassDB::bind_method(D_METHOD("get_schema_version"), &GameDefinition::get_schema_version);
    ClassDB::bind_method(D_METHOD("set_worlds", "worlds"), &GameDefinition::set_worlds);
    ClassDB::bind_method(D_METHOD("get_worlds"), &GameDefinition::get_worlds);
    ClassDB::bind_method(D_METHOD("set_starting_world", "starting_world"), &GameDefinition::set_starting_world);
    ClassDB::bind_method(D_METHOD("get_starting_world"), &GameDefinition::get_starting_world);
    ClassDB::bind_method(D_METHOD("set_starting_world_scene", "scene"), &GameDefinition::set_starting_world_scene);
    ClassDB::bind_method(D_METHOD("get_starting_world_scene"), &GameDefinition::get_starting_world_scene);
    ClassDB::bind_method(D_METHOD("set_player_scene", "scene"), &GameDefinition::set_player_scene);
    ClassDB::bind_method(D_METHOD("get_player_scene"), &GameDefinition::get_player_scene);
    ClassDB::bind_method(D_METHOD("set_multiplayer_rules", "rules"), &GameDefinition::set_multiplayer_rules);
    ClassDB::bind_method(D_METHOD("get_multiplayer_rules"), &GameDefinition::get_multiplayer_rules);
    ClassDB::bind_method(D_METHOD("set_service_provider", "provider"), &GameDefinition::set_service_provider);
    ClassDB::bind_method(D_METHOD("get_service_provider"), &GameDefinition::get_service_provider);

    ADD_PROPERTY(PropertyInfo(Variant::STRING, "app_id"), "set_app_id", "get_app_id");
    ADD_PROPERTY(PropertyInfo(Variant::STRING, "save_root"), "set_save_root", "get_save_root");
    ADD_PROPERTY(PropertyInfo(Variant::INT, "schema_version"), "set_schema_version", "get_schema_version");
    ADD_PROPERTY(PropertyInfo(Variant::DICTIONARY, "worlds"), "set_worlds", "get_worlds");
    ADD_PROPERTY(PropertyInfo(Variant::STRING_NAME, "starting_world"), "set_starting_world", "get_starting_world");
    ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "starting_world_scene", PROPERTY_HINT_RESOURCE_TYPE, "PackedScene"),
            "set_starting_world_scene", "get_starting_world_scene");
    ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "player_scene", PROPERTY_HINT_RESOURCE_TYPE, "PackedScene"),
            "set_player_scene", "get_player_scene");
    ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "multiplayer_rules", PROPERTY_HINT_RESOURCE_TYPE, "MultiplayerRules"),
            "set_multiplayer_rules", "get_multiplayer_rules");
    ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "service_provider", PROPERTY_HINT_RESOURCE_TYPE, "GameServiceProvider"),
            "set_service_provider", "get_service_provider");
}

Ref<GameDefinition> GameDefinition::configure() {
    Ref<GameDefinition> definition;
    definition.instantiate();
    return definition;
}

Ref<GameDefinition> GameDefinition::copy_definition() const {
    Ref<GameDefinition> definition = configure();
    definition->set_app_id(app_id);
    definition->set_save_root(save_root);
    definition->set_schema_version(schema_version);
    definition->set_worlds(worlds);
    definition->set_starting_world(starting_world);
    definition->set_starting_world_scene(starting_world_scene);
    definition->set_player_scene(player_scene);
    definition->set_service_provider(service_provider);
    if (multiplayer_rules.is_valid()) {
        definition->set_multiplayer_rules(multiplayer_rules->copy_rules());
    }
    return definition;
}

Ref<GameDefinition> GameDefinition::with_worlds(const Dictionary &p_worlds) const {
    Ref<GameDefinition> definition = copy_definition();
    definition->set_worlds(p_worlds);
    return definition;
}

Ref<GameDefinition> GameDefinition::starting_in(const StringName &p_world_id) const {
    Ref<GameDefinition> definition = copy_definition();
    definition->set_starting_world(p_world_id);
    return definition;
}

Ref<GameDefinition> GameDefinition::with_saves(
        const String &p_app_id, const String &p_root, int64_t p_schema_version) const {
    Ref<GameDefinition> definition = copy_definition();
    definition->set_app_id(p_app_id);
    definition->set_save_root(p_root);
    definition->set_schema_version(p_schema_version);
    return definition;
}

Ref<GameDefinition> GameDefinition::with_starting_world(
        const StringName &world_id, const Ref<PackedScene> &scene) const {
    Ref<GameDefinition> definition = copy_definition();
    definition->set_starting_world(world_id);
    definition->set_starting_world_scene(scene);
    return definition;
}

Ref<GameDefinition> GameDefinition::with_player(const Ref<PackedScene> &scene) const {
    Ref<GameDefinition> definition = copy_definition();
    definition->set_player_scene(scene);
    return definition;
}

Ref<GameDefinition> GameDefinition::with_multiplayer(const Ref<MultiplayerRules> &rules) const {
    Ref<GameDefinition> definition = copy_definition();
    definition->set_multiplayer_rules(rules.is_valid() ? rules->copy_rules() : Ref<MultiplayerRules>());
    return definition;
}

Ref<GameDefinition> GameDefinition::with_provider(const Ref<GameServiceProvider> &provider) const {
    Ref<GameDefinition> definition = copy_definition();
    definition->set_service_provider(provider);
    return definition;
}

GameRuntime *GameDefinition::create() const {
    auto *game = memnew(GameRuntime);
    game->set_name("Game");
    game->set_definition(copy_definition());
    return game;
}

GameRuntime *GameDefinition::boot(Node *owner) const {
    const OS *os = OS::get_singleton();
    if (!os || os->get_thread_caller_id() != os->get_main_thread_id() || !owner ||
            !owner->is_inside_tree() || owner->is_queued_for_deletion()) {
        return nullptr;
    }

    auto *game = create();
    const uint64_t game_id = game->get_instance_id();
    const uint64_t owner_id = owner->get_instance_id();
    owner->add_child(game);
    // Tree notifications can retire or move the child before add_child returns.
    game = Object::cast_to<GameRuntime>(ObjectDB::get_instance(game_id));
    if (!game) {
        return nullptr;
    }
    Node *parent = game->get_parent();
    if (!parent) {
        memdelete(game);
        return nullptr;
    }
    if (parent->get_instance_id() != owner_id || parent->is_queued_for_deletion() ||
            !game->is_inside_tree() || game->is_queued_for_deletion()) {
        return nullptr;
    }
    return game;
}

void GameDefinition::set_app_id(const String &p_app_id) {
    app_id = p_app_id;
}

String GameDefinition::get_app_id() const {
    return app_id;
}

void GameDefinition::set_save_root(const String &p_save_root) {
    save_root = p_save_root;
}

String GameDefinition::get_save_root() const {
    return save_root;
}

void GameDefinition::set_schema_version(int64_t p_schema_version) {
    schema_version = p_schema_version;
}

int64_t GameDefinition::get_schema_version() const {
    return schema_version;
}

void GameDefinition::set_worlds(const Dictionary &p_worlds) {
    worlds = p_worlds.duplicate(true);
}

Dictionary GameDefinition::get_worlds() const {
    return worlds.duplicate(true);
}

void GameDefinition::set_starting_world(const StringName &p_starting_world) {
    starting_world = p_starting_world;
}

StringName GameDefinition::get_starting_world() const {
    return starting_world;
}

void GameDefinition::set_starting_world_scene(const Ref<PackedScene> &scene) {
    starting_world_scene = scene;
}

Ref<PackedScene> GameDefinition::get_starting_world_scene() const {
    return starting_world_scene;
}

void GameDefinition::set_player_scene(const Ref<PackedScene> &scene) {
    player_scene = scene;
}

Ref<PackedScene> GameDefinition::get_player_scene() const {
    return player_scene;
}

void GameDefinition::set_multiplayer_rules(const Ref<MultiplayerRules> &rules) {
    multiplayer_rules = rules;
}

Ref<MultiplayerRules> GameDefinition::get_multiplayer_rules() const {
    return multiplayer_rules;
}

void GameDefinition::set_service_provider(const Ref<GameServiceProvider> &provider) {
    service_provider = provider;
}

Ref<GameServiceProvider> GameDefinition::get_service_provider() const {
    return service_provider;
}

} // namespace crimson::integration
