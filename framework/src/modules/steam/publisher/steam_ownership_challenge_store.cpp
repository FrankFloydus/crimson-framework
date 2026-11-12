// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#include "modules/steam/publisher/steam_ownership_challenge_store.hpp"

#include <godot_cpp/core/class_db.hpp>

namespace crimson::steam {

using namespace godot;

void SteamOwnershipChallengeStore::_bind_methods() {
    GDVIRTUAL_BIND(_consume, "app_id", "challenge", "steam_id", "issued_at_unix", "now_unix");
}

bool SteamOwnershipChallengeStore::consume(int64_t app_id, const PackedByteArray &challenge, const String &steam_id,
        int64_t issued_at_unix, int64_t now_unix) {
    bool accepted = false;
    return GDVIRTUAL_CALL(_consume, app_id, challenge, steam_id, issued_at_unix, now_unix, accepted) && accepted;
}

} // namespace crimson::steam
