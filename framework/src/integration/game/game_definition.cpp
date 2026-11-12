// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#include "integration/game/game_definition.hpp"

#include "integration/game/crimson_game.hpp"

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/core/memory.hpp>

namespace crimson::integration {

using namespace godot;

void CrimsonGameDefinition::_bind_methods() {
    ClassDB::bind_static_method(get_class_static(), D_METHOD("configure"), &CrimsonGameDefinition::configure);
    ClassDB::bind_method(D_METHOD("with_worlds", "worlds"), &CrimsonGameDefinition::with_worlds);
    ClassDB::bind_method(D_METHOD("starting_in", "world_id"), &CrimsonGameDefinition::starting_in);
    ClassDB::bind_method(D_METHOD("with_saves", "app_id", "root", "schema_version"), &CrimsonGameDefinition::with_saves,
            DEFVAL(String("user://crimson")), DEFVAL(1));
    ClassDB::bind_method(D_METHOD("with_starting_world", "world_id", "scene"), &CrimsonGameDefinition::with_starting_world);
    ClassDB::bind_method(D_METHOD("with_player", "scene"), &CrimsonGameDefinition::with_player);
    ClassDB::bind_method(D_METHOD("with_multiplayer", "rules"), &CrimsonGameDefinition::with_multiplayer);
    ClassDB::bind_method(D_METHOD("with_provider", "provider"), &CrimsonGameDefinition::with_provider);
    ClassDB::bind_method(D_METHOD("create"), &CrimsonGameDefinition::create);

    ClassDB::bind_method(D_METHOD("set_app_id", "app_id"), &CrimsonGameDefinition::set_app_id);
    ClassDB::bind_method(D_METHOD("get_app_id"), &CrimsonGameDefinition::get_app_id);
    ClassDB::bind_method(D_METHOD("set_save_root", "save_root"), &CrimsonGameDefinition::set_save_root);
    ClassDB::bind_method(D_METHOD("get_save_root"), &CrimsonGameDefinition::get_save_root);
    ClassDB::bind_method(D_METHOD("set_schema_version", "schema_version"), &CrimsonGameDefinition::set_schema_version);
    ClassDB::bind_method(D_METHOD("get_schema_version"), &CrimsonGameDefinition::get_schema_version);
    ClassDB::bind_method(D_METHOD("set_worlds", "worlds"), &CrimsonGameDefinition::set_worlds);
    ClassDB::bind_method(D_METHOD("get_worlds"), &CrimsonGameDefinition::get_worlds);
    ClassDB::bind_method(D_METHOD("set_starting_world", "starting_world"), &CrimsonGameDefinition::set_starting_world);
    ClassDB::bind_method(D_METHOD("get_starting_world"), &CrimsonGameDefinition::get_starting_world);
    ClassDB::bind_method(D_METHOD("set_starting_world_scene", "scene"), &CrimsonGameDefinition::set_starting_world_scene);
    ClassDB::bind_method(D_METHOD("get_starting_world_scene"), &CrimsonGameDefinition::get_starting_world_scene);
    ClassDB::bind_method(D_METHOD("set_player_scene", "scene"), &CrimsonGameDefinition::set_player_scene);
    ClassDB::bind_method(D_METHOD("get_player_scene"), &CrimsonGameDefinition::get_player_scene);
    ClassDB::bind_method(D_METHOD("set_multiplayer_rules", "rules"), &CrimsonGameDefinition::set_multiplayer_rules);
    ClassDB::bind_method(D_METHOD("get_multiplayer_rules"), &CrimsonGameDefinition::get_multiplayer_rules);
    ClassDB::bind_method(D_METHOD("set_service_provider", "provider"), &CrimsonGameDefinition::set_service_provider);
    ClassDB::bind_method(D_METHOD("get_service_provider"), &CrimsonGameDefinition::get_service_provider);

    ADD_PROPERTY(PropertyInfo(Variant::STRING, "app_id"), "set_app_id", "get_app_id");
    ADD_PROPERTY(PropertyInfo(Variant::STRING, "save_root"), "set_save_root", "get_save_root");
    ADD_PROPERTY(PropertyInfo(Variant::INT, "schema_version"), "set_schema_version", "get_schema_version");
    ADD_PROPERTY(PropertyInfo(Variant::DICTIONARY, "worlds"), "set_worlds", "get_worlds");
    ADD_PROPERTY(PropertyInfo(Variant::STRING_NAME, "starting_world"), "set_starting_world", "get_starting_world");
    ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "starting_world_scene", PROPERTY_HINT_RESOURCE_TYPE, "PackedScene"),
            "set_starting_world_scene", "get_starting_world_scene");
    ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "player_scene", PROPERTY_HINT_RESOURCE_TYPE, "PackedScene"),
            "set_player_scene", "get_player_scene");
    ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "multiplayer_rules", PROPERTY_HINT_RESOURCE_TYPE, "CrimsonMultiplayerRules"),
            "set_multiplayer_rules", "get_multiplayer_rules");
    ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "service_provider", PROPERTY_HINT_RESOURCE_TYPE, "CrimsonServiceProvider"),
            "set_service_provider", "get_service_provider");
}

Ref<CrimsonGameDefinition> CrimsonGameDefinition::configure() {
    Ref<CrimsonGameDefinition> definition;
    definition.instantiate();
    return definition;
}

Ref<CrimsonGameDefinition> CrimsonGameDefinition::copy_definition() const {
    Ref<CrimsonGameDefinition> definition = configure();
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

Ref<CrimsonGameDefinition> CrimsonGameDefinition::with_worlds(const Dictionary &p_worlds) const {
    Ref<CrimsonGameDefinition> definition = copy_definition();
    definition->set_worlds(p_worlds);
    return definition;
}

Ref<CrimsonGameDefinition> CrimsonGameDefinition::starting_in(const StringName &p_world_id) const {
    Ref<CrimsonGameDefinition> definition = copy_definition();
    definition->set_starting_world(p_world_id);
    return definition;
}

Ref<CrimsonGameDefinition> CrimsonGameDefinition::with_saves(
        const String &p_app_id, const String &p_root, int64_t p_schema_version) const {
    Ref<CrimsonGameDefinition> definition = copy_definition();
    definition->set_app_id(p_app_id);
    definition->set_save_root(p_root);
    definition->set_schema_version(p_schema_version);
    return definition;
}

Ref<CrimsonGameDefinition> CrimsonGameDefinition::with_starting_world(
        const StringName &world_id, const Ref<PackedScene> &scene) const {
    Ref<CrimsonGameDefinition> definition = copy_definition();
    definition->set_starting_world(world_id);
    definition->set_starting_world_scene(scene);
    return definition;
}

Ref<CrimsonGameDefinition> CrimsonGameDefinition::with_player(const Ref<PackedScene> &scene) const {
    Ref<CrimsonGameDefinition> definition = copy_definition();
    definition->set_player_scene(scene);
    return definition;
}

Ref<CrimsonGameDefinition> CrimsonGameDefinition::with_multiplayer(const Ref<CrimsonMultiplayerRules> &rules) const {
    Ref<CrimsonGameDefinition> definition = copy_definition();
    definition->set_multiplayer_rules(rules.is_valid() ? rules->copy_rules() : Ref<CrimsonMultiplayerRules>());
    return definition;
}

Ref<CrimsonGameDefinition> CrimsonGameDefinition::with_provider(const Ref<CrimsonServiceProvider> &provider) const {
    Ref<CrimsonGameDefinition> definition = copy_definition();
    definition->set_service_provider(provider);
    return definition;
}

CrimsonGame *CrimsonGameDefinition::create() const {
    auto *game = memnew(CrimsonGame);
    game->set_name("Game");
    game->set_definition(copy_definition());
    return game;
}

void CrimsonGameDefinition::set_app_id(const String &p_app_id) {
    app_id = p_app_id;
}

String CrimsonGameDefinition::get_app_id() const {
    return app_id;
}

void CrimsonGameDefinition::set_save_root(const String &p_save_root) {
    save_root = p_save_root;
}

String CrimsonGameDefinition::get_save_root() const {
    return save_root;
}

void CrimsonGameDefinition::set_schema_version(int64_t p_schema_version) {
    schema_version = p_schema_version;
}

int64_t CrimsonGameDefinition::get_schema_version() const {
    return schema_version;
}

void CrimsonGameDefinition::set_worlds(const Dictionary &p_worlds) {
    worlds = p_worlds.duplicate(true);
}

Dictionary CrimsonGameDefinition::get_worlds() const {
    return worlds.duplicate(true);
}

void CrimsonGameDefinition::set_starting_world(const StringName &p_starting_world) {
    starting_world = p_starting_world;
}

StringName CrimsonGameDefinition::get_starting_world() const {
    return starting_world;
}

void CrimsonGameDefinition::set_starting_world_scene(const Ref<PackedScene> &scene) {
    starting_world_scene = scene;
}

Ref<PackedScene> CrimsonGameDefinition::get_starting_world_scene() const {
    return starting_world_scene;
}

void CrimsonGameDefinition::set_player_scene(const Ref<PackedScene> &scene) {
    player_scene = scene;
}

Ref<PackedScene> CrimsonGameDefinition::get_player_scene() const {
    return player_scene;
}

void CrimsonGameDefinition::set_multiplayer_rules(const Ref<CrimsonMultiplayerRules> &rules) {
    multiplayer_rules = rules;
}

Ref<CrimsonMultiplayerRules> CrimsonGameDefinition::get_multiplayer_rules() const {
    return multiplayer_rules;
}

void CrimsonGameDefinition::set_service_provider(const Ref<CrimsonServiceProvider> &provider) {
    service_provider = provider;
}

Ref<CrimsonServiceProvider> CrimsonGameDefinition::get_service_provider() const {
    return service_provider;
}

} // namespace crimson::integration
