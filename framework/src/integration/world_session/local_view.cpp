// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#include "integration/world_session/local_view.hpp"

#include <godot_cpp/core/class_db.hpp>

namespace crimson::integration {

using namespace godot;

void CrimsonLocalView::_bind_methods() {
    ClassDB::bind_method(D_METHOD("set_camera", "camera"), &CrimsonLocalView::set_camera);
    ClassDB::bind_method(D_METHOD("get_camera"), &CrimsonLocalView::get_camera);
    ClassDB::bind_method(D_METHOD("set_hidden_visuals", "visuals"), &CrimsonLocalView::set_hidden_visuals);
    ClassDB::bind_method(D_METHOD("get_hidden_visuals"), &CrimsonLocalView::get_hidden_visuals);
    ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "camera", PROPERTY_HINT_NODE_TYPE, "Camera3D"),
            "set_camera", "get_camera");
    ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "hidden_visuals", PROPERTY_HINT_NODE_TYPE, "Node3D"),
            "set_hidden_visuals", "get_hidden_visuals");
}

void CrimsonLocalView::set_camera(Camera3D *camera) {
    camera_id = camera ? ObjectID(camera->get_instance_id()) : ObjectID();
}

Camera3D *CrimsonLocalView::get_camera() const {
    return Object::cast_to<Camera3D>(ObjectDB::get_instance(camera_id));
}

void CrimsonLocalView::set_hidden_visuals(Node3D *visuals) {
    hidden_visuals_id = visuals ? ObjectID(visuals->get_instance_id()) : ObjectID();
}

Node3D *CrimsonLocalView::get_hidden_visuals() const {
    return Object::cast_to<Node3D>(ObjectDB::get_instance(hidden_visuals_id));
}

} // namespace crimson::integration
