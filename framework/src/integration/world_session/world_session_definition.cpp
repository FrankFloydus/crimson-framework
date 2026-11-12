// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#include "integration/world_session/world_session_definition.hpp"

#include "integration/world_session/world_session.hpp"

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/core/memory.hpp>

namespace crimson::integration {

using namespace godot;

void CrimsonWorldSessionDefinition::_bind_methods() {
    ClassDB::bind_method(D_METHOD("with_world", "scene"), &CrimsonWorldSessionDefinition::with_world);
    ClassDB::bind_method(D_METHOD("with_players", "scene", "spawn_points_path"),
            &CrimsonWorldSessionDefinition::with_players, DEFVAL(NodePath("SpawnPoints")));
    ClassDB::bind_method(D_METHOD("with_local_view", "camera_path", "hidden_path"),
            &CrimsonWorldSessionDefinition::with_local_view, DEFVAL(NodePath()));
    ClassDB::bind_method(D_METHOD("on_port", "port"), &CrimsonWorldSessionDefinition::on_port);
    ClassDB::bind_method(D_METHOD("create"), &CrimsonWorldSessionDefinition::create);

    ClassDB::bind_method(D_METHOD("set_world_scene", "scene"), &CrimsonWorldSessionDefinition::set_world_scene);
    ClassDB::bind_method(D_METHOD("get_world_scene"), &CrimsonWorldSessionDefinition::get_world_scene);
    ClassDB::bind_method(D_METHOD("set_player_scene", "scene"), &CrimsonWorldSessionDefinition::set_player_scene);
    ClassDB::bind_method(D_METHOD("get_player_scene"), &CrimsonWorldSessionDefinition::get_player_scene);
    ClassDB::bind_method(D_METHOD("set_spawn_points_path", "path"),
            &CrimsonWorldSessionDefinition::set_spawn_points_path);
    ClassDB::bind_method(D_METHOD("get_spawn_points_path"), &CrimsonWorldSessionDefinition::get_spawn_points_path);
    ClassDB::bind_method(D_METHOD("set_local_camera_path", "path"),
            &CrimsonWorldSessionDefinition::set_local_camera_path);
    ClassDB::bind_method(D_METHOD("get_local_camera_path"), &CrimsonWorldSessionDefinition::get_local_camera_path);
    ClassDB::bind_method(D_METHOD("set_local_hidden_path", "path"),
            &CrimsonWorldSessionDefinition::set_local_hidden_path);
    ClassDB::bind_method(D_METHOD("get_local_hidden_path"), &CrimsonWorldSessionDefinition::get_local_hidden_path);
    ClassDB::bind_method(D_METHOD("set_port", "port"), &CrimsonWorldSessionDefinition::set_port);
    ClassDB::bind_method(D_METHOD("get_port"), &CrimsonWorldSessionDefinition::get_port);

    ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "world_scene", PROPERTY_HINT_RESOURCE_TYPE, "PackedScene"),
            "set_world_scene", "get_world_scene");
    ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "player_scene", PROPERTY_HINT_RESOURCE_TYPE, "PackedScene"),
            "set_player_scene", "get_player_scene");
    ADD_PROPERTY(PropertyInfo(Variant::NODE_PATH, "spawn_points_path"), "set_spawn_points_path", "get_spawn_points_path");
    ADD_PROPERTY(PropertyInfo(Variant::NODE_PATH, "local_camera_path"), "set_local_camera_path", "get_local_camera_path");
    ADD_PROPERTY(PropertyInfo(Variant::NODE_PATH, "local_hidden_path"), "set_local_hidden_path", "get_local_hidden_path");
    ADD_PROPERTY(PropertyInfo(Variant::INT, "port", PROPERTY_HINT_RANGE, "1,65535,1"), "set_port", "get_port");
}

Ref<CrimsonWorldSessionDefinition> CrimsonWorldSessionDefinition::configure() {
    Ref<CrimsonWorldSessionDefinition> definition;
    definition.instantiate();
    return definition;
}

Ref<CrimsonWorldSessionDefinition> CrimsonWorldSessionDefinition::copy_definition() const {
    Ref<CrimsonWorldSessionDefinition> copy = configure();
    copy->world_scene = world_scene;
    copy->player_scene = player_scene;
    copy->spawn_points_path = spawn_points_path;
    copy->local_camera_path = local_camera_path;
    copy->local_hidden_path = local_hidden_path;
    copy->port = port;
    return copy;
}

Ref<CrimsonWorldSessionDefinition> CrimsonWorldSessionDefinition::with_world(const Ref<PackedScene> &scene) const {
    Ref<CrimsonWorldSessionDefinition> copy = copy_definition();
    copy->set_world_scene(scene);
    return copy;
}

Ref<CrimsonWorldSessionDefinition> CrimsonWorldSessionDefinition::with_players(
        const Ref<PackedScene> &scene, const NodePath &spawn_path) const {
    Ref<CrimsonWorldSessionDefinition> copy = copy_definition();
    copy->set_player_scene(scene);
    copy->set_spawn_points_path(spawn_path);
    return copy;
}

Ref<CrimsonWorldSessionDefinition> CrimsonWorldSessionDefinition::with_local_view(
        const NodePath &camera_path, const NodePath &hidden_path) const {
    Ref<CrimsonWorldSessionDefinition> copy = copy_definition();
    copy->set_local_camera_path(camera_path);
    copy->set_local_hidden_path(hidden_path);
    return copy;
}

Ref<CrimsonWorldSessionDefinition> CrimsonWorldSessionDefinition::on_port(int64_t value) const {
    Ref<CrimsonWorldSessionDefinition> copy = copy_definition();
    copy->set_port(value);
    return copy;
}

CrimsonWorldSession *CrimsonWorldSessionDefinition::create() const {
    auto *session = memnew(CrimsonWorldSession);
    session->set_name("WorldSession");
    session->set_definition(copy_definition());
    return session;
}

void CrimsonWorldSessionDefinition::set_world_scene(const Ref<PackedScene> &scene) {
    world_scene = scene;
}

Ref<PackedScene> CrimsonWorldSessionDefinition::get_world_scene() const {
    return world_scene;
}

void CrimsonWorldSessionDefinition::set_player_scene(const Ref<PackedScene> &scene) {
    player_scene = scene;
}

Ref<PackedScene> CrimsonWorldSessionDefinition::get_player_scene() const {
    return player_scene;
}

void CrimsonWorldSessionDefinition::set_spawn_points_path(const NodePath &path) {
    spawn_points_path = path;
}

NodePath CrimsonWorldSessionDefinition::get_spawn_points_path() const {
    return spawn_points_path;
}

void CrimsonWorldSessionDefinition::set_local_camera_path(const NodePath &path) {
    local_camera_path = path;
}

NodePath CrimsonWorldSessionDefinition::get_local_camera_path() const {
    return local_camera_path;
}

void CrimsonWorldSessionDefinition::set_local_hidden_path(const NodePath &path) {
    local_hidden_path = path;
}

NodePath CrimsonWorldSessionDefinition::get_local_hidden_path() const {
    return local_hidden_path;
}

void CrimsonWorldSessionDefinition::set_port(int64_t value) {
    port = value;
}

int64_t CrimsonWorldSessionDefinition::get_port() const {
    return port;
}

} // namespace crimson::integration
