// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#pragma once

#include "modules/steam/client/steam_inventory_types.hpp"

#include <godot_cpp/core/object_id.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>

#include <cstdint>
#include <functional>
#include <memory>

namespace crimson::steam {

class SteamServerRuntime;
struct SteamInventoryVerifierState;

// Validates recent signed inventory selections on an authenticated Steam game server.
class SteamInventoryVerifier : public godot::RefCounted {
    GDCLASS(SteamInventoryVerifier, godot::RefCounted)

public:
    // Resolve the exact admitted identity for an authenticated Steam account.
    using AuthenticatedIdentity = std::function<godot::ObjectID(uint64_t)>;

    // Initialize a verifier without a game-server scope.
    SteamInventoryVerifier();
    // Release any remaining native proof observations.
    ~SteamInventoryVerifier() override;

    // Await verification for the expected SteamID64 of a currently admitted player.
    godot::Variant verify(const godot::PackedByteArray &proof, const godot::String &expected_steam_id);
    // Attach a native verifier to a dedicated runtime and exact admitted-identity resolver.
    void attach(const std::shared_ptr<SteamServerRuntime> &runtime, AuthenticatedIdentity resolver);
    // Advance results through the dedicated server's scene-thread update.
    void tick();
    // Release all native results and retire this verifier before notifying observers.
    void close();

protected:
    // Bind the directly awaited proof verification action.
    static void _bind_methods();

private:
    // Validate the completed signature's owner, age, and present item rows.
    godot::Ref<SteamInventorySnapshotResult> read_snapshot(int32_t handle, uint64_t account) const;

    // Observes the exact attached GameServer runtime without retaining it.
    std::weak_ptr<SteamServerRuntime> runtime;
    // Resolves a live SessionIdentity object for the authenticated peer.
    AuthenticatedIdentity authenticated_identity;
    // Native pending result handles and exact operation observations.
    std::unique_ptr<SteamInventoryVerifierState> native;
};

} // namespace crimson::steam
