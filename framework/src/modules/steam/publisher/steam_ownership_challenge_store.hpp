// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#pragma once

#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/core/gdvirtual.gen.inc>
#include <godot_cpp/variant/packed_byte_array.hpp>
#include <godot_cpp/variant/string.hpp>

namespace crimson::steam {

// Trusted durable authority for backend-issued, one-time encrypted ticket challenges.
class SteamOwnershipChallengeStore : public godot::RefCounted {
    GDCLASS(SteamOwnershipChallengeStore, godot::RefCounted)

protected:
    // Bind the atomic consume hook for the trusted application's challenge store.
    static void _bind_methods();
    // Atomically match an outstanding app-bound challenge and retire it on success.
    GDVIRTUAL5R(bool, _consume, int64_t, godot::PackedByteArray, godot::String, int64_t, int64_t)

private:
    friend class SteamPublisherAuth;

    // Consume only a challenge whose outstanding record accepts this verified identity and issue time.
    bool consume(int64_t app_id, const godot::PackedByteArray &challenge, const godot::String &steam_id,
            int64_t issued_at_unix, int64_t now_unix);
};

} // namespace crimson::steam
