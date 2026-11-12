// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#pragma once

namespace crimson::steam {

// Main-thread liveness shared by the asynchronous services from one client start.
struct SteamClientScope {
    // Stop clears this before any cancellation callback can call another service.
    bool active = true;
};

} // namespace crimson::steam
