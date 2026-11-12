// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#include "integration/world_session/world_session_definition.hpp"

#include "integration/world_session/world_session.hpp"

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/core/memory.hpp>

namespace crimson::integration {

using namespace godot;

void WorldSessionDefinition::_bind_methods() {
    ClassDB::bind_method(D_METHOD("with_world", "scene"), &WorldSessionDefinition::with_world);
    ClassDB::bind_method(D_METHOD("with_players", "scene", "spawn_points_path"),
            &WorldSessionDefinition::with_players, DEFVAL(NodePath("SpawnPoints")));
    ClassDB::bind_method(D_METHOD("with_local_view", "camera_path", "hidden_path"),
            &WorldSessionDefinition::with_local_view, DEFVAL(NodePath()));
    ClassDB::bind_method(D_METHOD("on_port", "port"), &WorldSessionDefinition::on_port);
    ClassDB::bind_method(D_METHOD("create"), &WorldSessionDefinition::create);

    ClassDB::bind_method(D_METHOD("set_world_scene", "scene"), &WorldSessionDefinition::set_world_scene);
    ClassDB::bind_method(D_METHOD("get_world_scene"), &WorldSessionDefinition::get_world_scene);
    ClassDB::bind_method(D_METHOD("set_player_scene", "scene"), &WorldSessionDefinition::set_player_scene);
    ClassDB::bind_method(D_METHOD("get_player_scene"), &WorldSessionDefinition::get_player_scene);
    ClassDB::bind_method(D_METHOD("set_spawn_points_path", "path"),
            &WorldSessionDefinition::set_spawn_points_path);
    ClassDB::bind_method(D_METHOD("get_spawn_points_path"), &WorldSessionDefinition::get_spawn_points_path);
    ClassDB::bind_method(D_METHOD("set_local_camera_path", "path"),
            &WorldSessionDefinition::set_local_camera_path);
    ClassDB::bind_method(D_METHOD("get_local_camera_path"), &WorldSessionDefinition::get_local_camera_path);
    ClassDB::bind_method(D_METHOD("set_local_hidden_path", "path"),
            &WorldSessionDefinition::set_local_hidden_path);
    ClassDB::bind_method(D_METHOD("get_local_hidden_path"), &WorldSessionDefinition::get_local_hidden_path);
    ClassDB::bind_method(D_METHOD("set_port", "port"), &WorldSessionDefinition::set_port);
    ClassDB::bind_method(D_METHOD("get_port"), &WorldSessionDefinition::get_port);

    ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "world_scene", PROPERTY_HINT_RESOURCE_TYPE, "PackedScene"),
            "set_world_scene", "get_world_scene");
    ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "player_scene", PROPERTY_HINT_RESOURCE_TYPE, "PackedScene"),
            "set_player_scene", "get_player_scene");
    ADD_PROPERTY(PropertyInfo(Variant::NODE_PATH, "spawn_points_path"), "set_spawn_points_path", "get_spawn_points_path");
    ADD_PROPERTY(PropertyInfo(Variant::NODE_PATH, "local_camera_path"), "set_local_camera_path", "get_local_camera_path");
    ADD_PROPERTY(PropertyInfo(Variant::NODE_PATH, "local_hidden_path"), "set_local_hidden_path", "get_local_hidden_path");
    ADD_PROPERTY(PropertyInfo(Variant::INT, "port", PROPERTY_HINT_RANGE, "1,65535,1"), "set_port", "get_port");
}

Ref<WorldSessionDefinition> WorldSessionDefinition::configure() {
    Ref<WorldSessionDefinition> definition;
    definition.instantiate();
    return definition;
}

Ref<WorldSessionDefinition> WorldSessionDefinition::copy_definition() const {
    Ref<WorldSessionDefinition> copy = configure();
    copy->world_scene = world_scene;
    copy->player_scene = player_scene;
    copy->spawn_points_path = spawn_points_path;
    copy->local_camera_path = local_camera_path;
    copy->local_hidden_path = local_hidden_path;
    copy->port = port;
    return copy;
}

Ref<WorldSessionDefinition> WorldSessionDefinition::with_world(const Ref<PackedScene> &scene) const {
    Ref<WorldSessionDefinition> copy = copy_definition();
    copy->set_world_scene(scene);
    return copy;
}

Ref<WorldSessionDefinition> WorldSessionDefinition::with_players(
        const Ref<PackedScene> &scene, const NodePath &spawn_path) const {
    Ref<WorldSessionDefinition> copy = copy_definition();
    copy->set_player_scene(scene);
    copy->set_spawn_points_path(spawn_path);
    return copy;
}

Ref<WorldSessionDefinition> WorldSessionDefinition::with_local_view(
        const NodePath &camera_path, const NodePath &hidden_path) const {
    Ref<WorldSessionDefinition> copy = copy_definition();
    copy->set_local_camera_path(camera_path);
    copy->set_local_hidden_path(hidden_path);
    return copy;
}

Ref<WorldSessionDefinition> WorldSessionDefinition::on_port(int64_t value) const {
    Ref<WorldSessionDefinition> copy = copy_definition();
    copy->set_port(value);
    return copy;
}

WorldSession *WorldSessionDefinition::create() const {
    auto *session = memnew(WorldSession);
    session->set_name("WorldSession");
    session->set_definition(copy_definition());
    return session;
}

void WorldSessionDefinition::set_world_scene(const Ref<PackedScene> &scene) {
    world_scene = scene;
}

Ref<PackedScene> WorldSessionDefinition::get_world_scene() const {
    return world_scene;
}

void WorldSessionDefinition::set_player_scene(const Ref<PackedScene> &scene) {
    player_scene = scene;
}

Ref<PackedScene> WorldSessionDefinition::get_player_scene() const {
    return player_scene;
}

void WorldSessionDefinition::set_spawn_points_path(const NodePath &path) {
    spawn_points_path = path;
}

NodePath WorldSessionDefinition::get_spawn_points_path() const {
    return spawn_points_path;
}

void WorldSessionDefinition::set_local_camera_path(const NodePath &path) {
    local_camera_path = path;
}

NodePath WorldSessionDefinition::get_local_camera_path() const {
    return local_camera_path;
}

void WorldSessionDefinition::set_local_hidden_path(const NodePath &path) {
    local_hidden_path = path;
}

NodePath WorldSessionDefinition::get_local_hidden_path() const {
    return local_hidden_path;
}

void WorldSessionDefinition::set_port(int64_t value) {
    port = value;
}

int64_t WorldSessionDefinition::get_port() const {
    return port;
}

} // namespace crimson::integration
