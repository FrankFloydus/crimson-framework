// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#include "register_session.hpp"

#include "game_session.hpp"
#include "online_listing.hpp"

#include <godot_cpp/core/class_db.hpp>

namespace crimson::session {

void register_types(godot::ModuleInitializationLevel level) {
    if (level == godot::MODULE_INITIALIZATION_LEVEL_SCENE) {
        GDREGISTER_CLASS(SessionEndpoint);
        GDREGISTER_CLASS(SessionTransportResult);
        GDREGISTER_CLASS(SessionTransport);
        GDREGISTER_CLASS(ENetSessionTransport);
        GDREGISTER_CLASS(SessionIdentity);
        GDREGISTER_CLASS(SessionAuthRequest);
        GDREGISTER_CLASS(SessionAuthenticator);
        GDREGISTER_VIRTUAL_CLASS(GameSession);
        GDREGISTER_CLASS(OnlineListing);
    }
}

void unregister_types(godot::ModuleInitializationLevel) {
    // godot-cpp unregisters the class at extension shutdown.
}

} // namespace crimson::session
