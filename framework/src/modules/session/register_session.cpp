// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#include "register_session.hpp"

#include "game_session.hpp"

#include <godot_cpp/core/class_db.hpp>

namespace crimson::session {

void register_types(godot::ModuleInitializationLevel level) {
    if (level == godot::MODULE_INITIALIZATION_LEVEL_SCENE) {
        GDREGISTER_CLASS(CrimsonSessionEndpoint);
        GDREGISTER_CLASS(CrimsonSessionTransportResult);
        GDREGISTER_CLASS(CrimsonSessionTransport);
        GDREGISTER_CLASS(CrimsonENetSessionTransport);
        GDREGISTER_CLASS(CrimsonSessionIdentity);
        GDREGISTER_CLASS(CrimsonSessionAuthRequest);
        GDREGISTER_CLASS(CrimsonSessionAuthenticator);
        GDREGISTER_CLASS(CrimsonGameSession);
    }
}

void unregister_types(godot::ModuleInitializationLevel) {
    // godot-cpp unregisters the class at extension shutdown.
}

} // namespace crimson::session
