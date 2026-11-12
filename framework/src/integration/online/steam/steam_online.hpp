// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#pragma once

#include "integration/game/game_services.hpp"

#ifdef CRIMSON_WITH_STEAM
#include "integration/online/steam/steam_transport.hpp"

#include <map>
#endif

namespace crimson::integration {

#ifdef CRIMSON_WITH_STEAM

class CrimsonSteamAuthenticator;

// A persistent scene-thread pump for the selected Steam client's SDK callbacks.
class CrimsonSteamRuntimeNode : public godot::Node {
    GDCLASS(CrimsonSteamRuntimeNode, godot::Node)

    // Keeps the shared Steam runtime and local-account reservation alive.
    std::shared_ptr<SteamRuntime> runtime;
    // Authenticator in the same Session, observed weakly after callback dispatch.
    godot::ObjectID authenticator_id;

protected:
    // Internal node has no script methods.
    static void _bind_methods();

public:
    // Transfer an already acquired local-account reservation to this node.
    void configure(const std::shared_ptr<SteamRuntime> &sdk, CrimsonSteamAuthenticator &authenticator);
    // Release the scope reservation when its persistent Session is destroyed.
    ~CrimsonSteamRuntimeNode() override;
    // Pump the SDK while this game is attached, including before a match starts.
    void _process(double delta) override;
};

// Verifies identity using the certificate on the actual Steam P2P connection.
class CrimsonSteamAuthenticator : public session::CrimsonSessionAuthenticator {
    GDCLASS(CrimsonSteamAuthenticator, session::CrimsonSessionAuthenticator)

    // Adapter that owns the current authenticated Steam connection mapping.
    godot::Ref<CrimsonSteamSessionTransport> transport;
    // Shared SDK account used to frame the local reply.
    std::shared_ptr<SteamRuntime> runtime;
    // One verified Steam connection waiting briefly for the host's lobby roster update.
    struct PendingMembership {
        // Exact native pending authentication attempt.
        godot::Ref<session::CrimsonSessionAuthRequest> request;
        // Transport-certified Steam account on that connection.
        uint64_t account = 0;
        // Monotonic time after which missing membership is denied.
        uint64_t deadline = 0;
    };
    // Bounded pending checks keyed by the actual Godot peer ID.
    std::map<int32_t, PendingMembership> pending;

protected:
    // Answer the nonce on a connection with a verified transport identity.
    godot::PackedByteArray make_proof(int32_t peer_id, bool authority,
            const godot::PackedByteArray &challenge) override;
    // Verify the proof against the exact native remote identity.
    void verify(const godot::Ref<session::CrimsonSessionAuthRequest> &request) override;
    // Internal adapter has no additional script methods.
    static void _bind_methods();

public:
    // Inject the selected runtime and transport before installing the authenticator.
    void configure(const std::shared_ptr<SteamRuntime> &sdk,
            const godot::Ref<CrimsonSteamSessionTransport> &selected_transport);
    // Settle an authenticated private-lobby guest after SDK roster propagation.
    void poll_membership();
};

// Private-lobby publication, resolution, and invite routing for one Steam game scope.
class CrimsonSteamOnlineDirectory : public CrimsonOnlineDirectory {
    GDCLASS(CrimsonSteamOnlineDirectory, CrimsonOnlineDirectory)

    // Platform runtime shared with the current Session and transport.
    std::shared_ptr<SteamRuntime> runtime;
    // Transport exposing the current mounted authority's actual Steam endpoint.
    godot::Ref<CrimsonSteamSessionTransport> transport;
    // Configured matchmaking discriminator for this Crimson game.
    godot::String lobby_namespace;
    // Virtual port pinned by the attached Steam transport.
    int32_t virtual_port = 0;
    // Weak destination for native invite notifications.
    godot::ObjectID provider_id;
    // Steam lobby that this scope owns as host, if any.
    uint64_t hosted = 0;
    // Steam lobby that this scope joined while resolving a route, if any.
    uint64_t joined = 0;
    // One in-flight Steam lobby call per game scope.
    bool pending = false;
    // Manual dispatch subscription for invite notices.
    uint64_t invite_subscription = 0;

    // Check the create result, publish verified metadata and complete the exact request.
    void finish_publish(const void *data, const godot::StringName &world,
            const godot::Ref<CrimsonOnlineDirectoryRequest> &request);
    // Check the joined lobby's actual owner and complete the Steam route.
    void finish_resolve(const void *data, uint64_t expected_lobby,
            const godot::Ref<CrimsonOnlineDirectoryRequest> &request);
    // Observe a native invite without changing the current game's lifecycle.
    void invite_received(const void *data, size_t size);

protected:
    // Create a private lobby for the already mounted Steam authority.
    void begin_publish(const godot::Ref<session::CrimsonSessionEndpoint> &endpoint,
            const godot::StringName &world, const godot::Ref<CrimsonOnlineDirectoryRequest> &request) override;
    // Join the invited lobby and resolve its immutable authority route.
    void begin_resolve(const godot::String &listing_id,
            const godot::Ref<CrimsonOnlineDirectoryRequest> &request) override;
    // Close and leave this scope's hosted lobby.
    void remove_listing(const godot::String &listing_id) override;
    // Leave membership owned by the current guest.
    void leave_listing(const godot::String &listing_id) override;
    // Internal directory has no additional script methods.
    static void _bind_methods();

public:
    // Share the selected transport, SDK, and provider-scoped lobby namespace.
    void configure(const std::shared_ptr<SteamRuntime> &sdk,
            const godot::Ref<CrimsonSteamSessionTransport> &selected_transport,
            const godot::String &selected_namespace, int32_t selected_port, godot::ObjectID provider);
    // Release Steam lobby ownership and the invite subscription.
    ~CrimsonSteamOnlineDirectory() override;
    // Request the active host's invite dialog once the Steam overlay is ready.
    godot::Error open_invite_overlay();
    // Submit an invitation to one validated Steam account from the current private lobby.
    godot::Error invite_player(uint64_t invitee);
};

#endif

// Selects the native Steam identity, directory, and P2P transport for a Crimson game.
class CrimsonSteamProvider : public CrimsonServiceProvider {
    GDCLASS(CrimsonSteamProvider, CrimsonServiceProvider)

    // Optional expected AppID. Zero accepts the one selected by the Steam client.
    int64_t app_id = 0;
    // Steam virtual port used by the native host.
    int64_t virtual_port = 0;
    // Stable game namespace advertised in shared app and lobby metadata.
    godot::String lobby_namespace = "crimson";
    // Most recent provider startup error, empty on a successful attachment.
    godot::String initialization_error;

#ifdef CRIMSON_WITH_STEAM
    // Weak process runtime shared with the current game scope.
    std::weak_ptr<SteamRuntime> runtime;
    // Weak transport selected for the current game scope.
    godot::ObjectID transport_id;
    // Weak directory used for the current host's invitation actions.
    godot::ObjectID directory_id;
#endif

protected:
    // Build a detached Steam-aware Session while preserving the offline fallback.
    session::CrimsonGameSession *create_session() override;
    // Construct the directory for the same captured Steam services.
    godot::Ref<CrimsonOnlineDirectory> create_online_directory() override;
    // Expose configuration, readiness, and explicit invitation actions to Godot.
    static void _bind_methods();

public:
    // Set the optional expected AppID before the game attaches.
    void set_app_id(int64_t value);
    // Read the optional expected AppID.
    int64_t get_app_id() const;
    // Set the native Steam virtual port for this game's listener.
    void set_virtual_port(int64_t value);
    // Read the configured native Steam virtual port.
    int64_t get_virtual_port() const;
    // Set a short alphanumeric lobby namespace before the game attaches.
    void set_lobby_namespace(const godot::String &value);
    // Read the current game namespace used for lobby lookup and publication.
    godot::String get_lobby_namespace() const;
    // Report whether the attached provider has a live native Steam account.
    bool is_ready() const;
    // Read the signed-in account ID as a decimal string, or empty when unavailable.
    godot::String get_local_steam_id() const;
    // Read the diagnostic for a failed Steam service attachment.
    godot::String get_initialization_error() const;
    // Request the invite dialog for the published game, or return ERR_BUSY while the overlay loads.
    godot::Error open_invite_overlay();
    // Submit a private lobby invitation to the explicitly selected Steam account.
    godot::Error invite_player(const godot::String &steam_id);
};

} // namespace crimson::integration
