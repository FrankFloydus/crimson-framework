// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#include "modules/session/online_listing.hpp"

#include <godot_cpp/core/class_db.hpp>

namespace crimson::session {
using namespace godot;

void OnlineListing::_bind_methods() {
    ClassDB::bind_method(D_METHOD("get_provider"), &OnlineListing::get_provider);
    ClassDB::bind_method(D_METHOD("get_id"), &OnlineListing::get_id);
    ADD_PROPERTY(PropertyInfo(Variant::STRING_NAME, "provider", PROPERTY_HINT_NONE, "",
                         PROPERTY_USAGE_NO_INSTANCE_STATE | PROPERTY_USAGE_READ_ONLY),
            "", "get_provider");
    ADD_PROPERTY(PropertyInfo(Variant::STRING, "id", PROPERTY_HINT_NONE, "",
                         PROPERTY_USAGE_NO_INSTANCE_STATE | PROPERTY_USAGE_READ_ONLY),
            "", "get_id");
}

StringName OnlineListing::get_provider() const {
    return provider;
}

String OnlineListing::get_id() const {
    return id;
}

void OnlineListing::set_locator(const StringName &selected_provider, const String &listing_id) {
    provider = selected_provider;
    id = listing_id;
}

} // namespace crimson::session
