// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#pragma once

#include "integration/game/game_services.hpp"
#include "modules/steam/lobbies/steam_lobbies.hpp"
#include "modules/steam/server/steam_server.hpp"
#include "modules/steam/client/steam_client_scope.hpp"

#ifdef CRIMSON_WITH_STEAM
#include "integration/online/steam/steam_transport.hpp"

#include <map>
#endif

namespace crimson::steam {
struct SteamPartyHostState;
struct SteamMessageGamePeer;
class SteamApps;
class SteamClient;
class Steamworks;
class SteamCloud;
class SteamDiagnostics;
class SteamFriends;
class SteamHttp;
class SteamInventory;
class SteamLeaderboards;
class SteamOverlay;
class SteamRemotePlay;
class SteamScreenshots;
class SteamStats;
class SteamTimeline;
class SteamUtils;
class SteamWorkshop;
class SteamPresence;
class SteamMusic;
class SteamVideo;
class SteamDevice;
class SteamAuth;
class SteamInput;
class SteamBrowser;
class SteamServerBrowser;
class SteamCommerceBackend;
class SteamPurchases;
class SteamVoice;
class SteamParties;
class SteamMessages;
class SteamNetworking;
class SteamSockets;
}

namespace crimson::integration {

using steam::SteamListing;
using steam::SteamLobbies;

class SteamOnlineDirectory;
class SteamDedicatedServer;
class SteamTicketVerifier;
class SteamRelayCoordinator;
class SteamRelayRequest;

// Detached invitation observed from the Steam client or launch arguments.
class SteamInvite : public godot::RefCounted {
    GDCLASS(SteamInvite, godot::RefCounted)

public:
    // Return the provider-owned locator for a normal GameRuntime join.
    godot::Ref<SteamListing> get_listing() const;
    // Return the inviter's SteamID64 when Valve supplies it.
    godot::String get_from_steam_id() const;

protected:
    // Register the immutable invitation fields.
    static void _bind_methods();

private:
    friend class GameInvites;

    // Detached lobby locator captured by the exact Game attachment.
    godot::Ref<SteamListing> listing;
    // Inviter identity when provided by a typed Steam callback.
    godot::String from_steam_id;
};

// Weak gateway to one GameRuntime attachment's invites.
class GameInvites : public godot::RefCounted {
    GDCLASS(GameInvites, godot::RefCounted)

public:
    // Return whether the exact owning Game has an active invite backend.
    bool is_available() const;
    // Request the current hosted lobby's Steam invite picker.
    godot::Error open_overlay();
    // Submit one invite to the explicitly selected Steam account.
    godot::Error send(const godot::String &steam_id);
    // Return the latest invite received in this attachment.
    godot::Ref<SteamInvite> pending() const;
    // Clear only the exact pending offer presented to the player.
    bool discard(const godot::Ref<SteamInvite> &invite);

protected:
    // Register invitation actions and the typed received signal.
    static void _bind_methods();

private:
    friend class SteamOnlineDirectory;

    // Exact directory identity, invalidated before teardown signals.
    godot::ObjectID directory_id;
    // Last valid invite, owned until this attachment retires.
    godot::Ref<SteamInvite> last_invite;
    // Last observed locator, retained briefly even after acceptance or dismissal.
    godot::String last_invite_listing;
    // Monotonic timestamp for short-lived duplicate notifications.
    uint64_t last_invite_msec = 0;

    // Capture a provider-owned locator before notifying observers.
    bool receive(const godot::Ref<SteamListing> &listing, const godot::String &sender);
    // Drop the attachment and its startup invite.
    void detach();
};

// Live capability view of an exact game attachment, including startup failures.
class SteamScope : public godot::RefCounted {
    GDCLASS(SteamScope, godot::RefCounted)

public:
    // Report whether the game attachment has a usable Steam client.
    bool is_ready() const;
    // Return the confirmed Steam AppID, or zero when unavailable.
    int64_t get_app_id() const;
    // Return the signed-in SteamID64, or empty when unavailable.
    godot::String get_local_steam_id() const;
    // Return the diagnostic captured when this Game attachment started.
    godot::String get_initialization_error() const;
    // Resolve the exact attachment's lobby discovery service.
    godot::Ref<SteamLobbies> lobbies() const;
    // Resolve the current Game start's dedicated server browser.
    godot::Ref<steam::SteamServerBrowser> server_browser() const;
    // Resolve the current start's Steam app metadata service.
    godot::Ref<steam::SteamApps> apps() const;
    // Resolve the current start's Steam friends service.
    godot::Ref<steam::SteamFriends> friends() const;
    // Resolve the current start's Steam overlay service, when available.
    godot::Ref<steam::SteamOverlay> overlay() const;
    // Resolve the current start's Steam utility service.
    godot::Ref<steam::SteamUtils> utils() const;
    // Resolve the current start's diagnostics service.
    godot::Ref<steam::SteamDiagnostics> diagnostics() const;
    // Resolve the current start's Steam HTTP service.
    godot::Ref<steam::SteamHttp> http() const;
    // Resolve the current start's stats and achievements service.
    godot::Ref<steam::SteamStats> stats() const;
    // Resolve the current start's leaderboards service.
    godot::Ref<steam::SteamLeaderboards> leaderboards() const;
    // Resolve the current start's Steam Cloud service.
    godot::Ref<steam::SteamCloud> cloud() const;
    // Resolve the current start's Steam Timeline service.
    godot::Ref<steam::SteamTimeline> timeline() const;
    // Resolve this attachment's screenshot capture and result service.
    godot::Ref<steam::SteamScreenshots> screenshots() const;
    // Resolve this attachment's Remote Play session and input service.
    godot::Ref<steam::SteamRemotePlay> remote_play() const;
    // Resolve this attachment's Steam Inventory service.
    godot::Ref<steam::SteamInventory> inventory() const;
    // Resolve this attachment's Steam Workshop service.
    godot::Ref<steam::SteamWorkshop> workshop() const;
    // Resolve this attachment's managed Rich Presence service.
    godot::Ref<steam::SteamPresence> presence() const;
    // Resolve this attachment's Steam music player service.
    godot::Ref<steam::SteamMusic> music() const;
    // Resolve this attachment's Steam video and broadcast service.
    godot::Ref<steam::SteamVideo> video() const;
    // Resolve this attachment's device, virtual keyboard, and VR status service.
    godot::Ref<steam::SteamDevice> device() const;
    // Resolve this attachment's authentication and ownership ticket service.
    godot::Ref<steam::SteamAuth> auth() const;
    // Resolve this attachment's Steam Input controller service.
    godot::Ref<steam::SteamInput> input() const;
    // Resolve this attachment's Steam HTML Surface service.
    godot::Ref<steam::SteamBrowser> browser() const;
    // Resolve this attachment's authenticated purchase facade.
    godot::Ref<steam::SteamPurchases> purchases() const;
    // Resolve this attachment's Steam voice service.
    godot::Ref<steam::SteamVoice> voice() const;
    // Resolve this attachment's Steam party beacons and reservations.
    godot::Ref<steam::SteamParties> parties() const;
    // Resolve this attachment's authorized Steam message service.
    godot::Ref<steam::SteamMessages> messages() const;
    // Resolve this attachment's Steam relay and connection-path diagnostics.
    godot::Ref<steam::SteamNetworking> networking() const;
    // Resolve this attachment's standalone Steam Sockets service.
    godot::Ref<steam::SteamSockets> sockets() const;

protected:
    // Register the game-scoped capability reads.
    static void _bind_methods();

private:
    friend class SteamOnlineDirectory;
    friend godot::Ref<steam::Steamworks> steamworks_for_game(GameRuntime *game);

    // Liveness token retired on Game exit even when this facade is retained.
    std::shared_ptr<steam::SteamClientScope> attachment;
    // Weak identity of the persistent Session's internal SteamClient.
    godot::ObjectID client_id;
    // Lobby service tied to this exact attachment.
    godot::Ref<SteamLobbies> lobby_service;
    // Startup error captured by the owning Session.
    godot::String initialization_error;

    // Resolve the internal client only for this exact active start.
    steam::SteamClient *resolve_client() const;
};

// Resolves the platform binding captured by the game service container.
class SteamServices : public godot::Object {
    GDCLASS(SteamServices, godot::Object)

public:
    // Return the exact published game's Steam scope, including a failed binding.
    static godot::Ref<SteamScope> for_game(GameRuntime *game);
    // Resolve Steam for the selected or sole active game.
    static godot::Ref<SteamScope> current();
    // Return the selected Game's exact dedicated server status, if configured.
    static godot::Ref<SteamDedicatedServer> server_for(GameRuntime *game);

protected:
    // Register the static typed scope accessors.
    static void _bind_methods();
};

#ifdef CRIMSON_WITH_STEAM

class SteamAuthenticator;
class SteamGameScope;

// A persistent scene-thread pump for the selected Steam client's SDK callbacks.
class SteamRuntimeNode : public godot::Node {
    GDCLASS(SteamRuntimeNode, godot::Node)

    // Owns the exact local-account game reservation until this node is destroyed.
    std::shared_ptr<SteamGameScope> game_scope;
    // Authenticator in the same Session, observed weakly after callback dispatch.
    godot::ObjectID authenticator_id;
    // Persistent internal client which opens a new service scope per tree attachment.
    godot::ObjectID client_id;
    // Fatal IPC subscription owned by the current Game runtime.
    uint64_t ipc_subscription = 0;

    // Retire the failed Steam generation without terminating offline play.
    static void on_ipc_failure(const std::shared_ptr<SteamGameScope> &scope, godot::ObjectID authenticator);

protected:
    // Internal node has no script methods.
    static void _bind_methods();

public:
    // Transfer an already acquired local-account reservation to this node.
    void configure(const std::shared_ptr<SteamGameScope> &scope, godot::ObjectID authenticator);
    // Return the persistent binding selected for this Session at creation.
    const std::shared_ptr<SteamGameScope> &get_game_scope() const;
    // Resolve the internal client attached to this Session's runtime node.
    steam::SteamClient *get_client() const;
    // Release the scope reservation when its persistent Session is destroyed.
    ~SteamRuntimeNode() override;
    // Pump the SDK while this game is attached, including before a match starts.
    void _process(double delta) override;
};

// Verifies identity using the certificate on the actual Steam P2P connection.
class SteamAuthenticator : public session::SessionAuthenticator {
    GDCLASS(SteamAuthenticator, session::SessionAuthenticator)

    // Adapter that owns the current authenticated Steam connection mapping.
    godot::Ref<SteamSessionTransport> transport;
    // SDK identity and the exact hosted lobby used for admission.
    std::shared_ptr<SteamGameScope> game_scope;
    // Authenticates Steam tickets on the exact signed-in listen authority pipe.
    std::shared_ptr<SteamTicketVerifier> verifier;
    // One verified Steam connection waiting briefly for the host's lobby roster update.
    struct PendingMembership {
        // Exact native pending authentication attempt.
        godot::Ref<session::SessionAuthRequest> request;
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
    void verify(const godot::Ref<session::SessionAuthRequest> &request) override;
    // Internal adapter has no additional script methods.
    static void _bind_methods();

public:
    // Inject the exact Game Session, transport, and signed-in ticket verifier.
    bool configure(const std::shared_ptr<SteamGameScope> &scope,
            const godot::Ref<SteamSessionTransport> &selected_transport, session::GameSession &session);
    // Retire the persistent Game owner without discarding unresolved Steam callbacks.
    void detach_owner();
    // Retire the owner if this authenticator is destroyed before its runtime node.
    ~SteamAuthenticator() override;
    // Settle an authenticated private-lobby guest after SDK roster propagation.
    void poll_membership();
};

#endif

// Steam lobby publication, resolution, and invite routing for one Game attachment.
class SteamOnlineDirectory : public OnlineDirectory {
    GDCLASS(SteamOnlineDirectory, OnlineDirectory)

#ifdef CRIMSON_WITH_STEAM
    friend class SteamRuntimeNode;
#endif
    // Game-facing capability view for this attachment.
    godot::Ref<SteamScope> steam_scope;
    // Game invite gateway with weak access to this directory.
    godot::Ref<GameInvites> invite_gateway;
    // Attachment cleanup must run even after the shared client token retires.
    bool retired = false;

#ifdef CRIMSON_WITH_STEAM
    // Shares the current host's publication with its authenticator.
    std::shared_ptr<SteamGameScope> game_scope;
    // Transport exposing the current mounted authority's actual Steam endpoint.
    godot::Ref<SteamSessionTransport> transport;
    // Configured matchmaking discriminator for this Crimson game.
    godot::String lobby_namespace;
    // Virtual port pinned by the attached Steam transport.
    int32_t virtual_port = 0;
    // Visibility selected before this attachment opened.
    int32_t visibility = 0;
    // Weak destination for native invite notifications.
    godot::ObjectID provider_id;
    // Steam lobby that this scope owns as host, if any.
    uint64_t hosted = 0;
    // Steam lobby that this scope joined while resolving a route, if any.
    uint64_t joined = 0;
    // Exact directory listing whose client ticket belongs to this attachment.
    godot::String ticket_listing_id;
    // One exact backend hosted authorization awaiting native ticket validation.
    godot::Ref<SteamRelayRequest> relay_authorization;
    // The Game directory resolve operation waiting for that hosted authorization.
    godot::Ref<OnlineDirectoryRequest> relay_resolution;
    // Exact process account reservation while acquiring or holding Steam lobby membership.
    std::shared_ptr<steam::SteamLobbyLease> membership;
    // Whether this attachment is still awaiting its create or join result.
    bool pending = false;
    // Manual dispatch subscription for invite notices.
    uint64_t invite_subscription = 0;
    // Manual dispatch subscription for Steam Rich Presence joins.
    uint64_t presence_subscription = 0;
    // Manual dispatch subscription for Steam-provided URL arguments while already running.
    uint64_t launch_subscription = 0;
    // Exact connect string this attachment published and may retire.
    godot::String owned_connect_presence;
    // Explicit suppression applies only to the currently hosted lobby.
    uint64_t suppressed_presence_lobby = 0;
    // Bound retry frequency if Steam or another owner declined publication.
    uint64_t next_presence_attempt_ms = 0;
    // Party capability of this exact Game's signed-in client scope.
    godot::Ref<steam::SteamParties> party_service;
    // Read owned joinability, free seats and Steam-certified world-ready guests.
    steam::SteamPartyHostState party_host_state() const;
    // Read the authenticated Game identity and current world for a Messages binding.
    steam::SteamMessageGamePeer message_game_peer(int64_t peer_id) const;
    // Check the create result, publish verified metadata and complete the exact request.
    void finish_publish(uint64_t lobby, const godot::StringName &world,
            const godot::Ref<OnlineDirectoryRequest> &request);
    // Check the joined lobby's actual owner and complete the Steam route.
    void finish_resolve(uint64_t lobby, uint64_t expected_lobby, uint64_t expected_owner, const godot::String &listing,
            const godot::Ref<OnlineDirectoryRequest> &request);
    // Observe a native invite without changing the current game's lifecycle.
    void invite_received(const void *data, size_t size);
    // Observe Steam's accepted Rich Presence join as an explicit typed invite.
    void presence_join_received(const void *data, size_t size);
    // Observe a Steam URL launch without executing or joining arbitrary launch arguments.
    void launch_join_received();
    // Report the failed SDK generation and release the Game attachment's native resources.
    void retire_after_ipc_failure();
    // Deliver one validated lobby candidate through the exact game gateway.
    void receive_lobby_candidate(uint64_t lobby, uint64_t sender);
    // Publish a join route only while the exact eligible game can admit a guest.
    void poll_join_presence();
    // Check the exact published Game and derive its Steam join route.
    godot::String eligible_join_route() const;
    // Apply the Game-scoped public Join Game action, or suppress its current lobby when null.
    godot::Error set_join_presence(const godot::Ref<SteamListing> &listing);
    // Set the eligible route only when the Steam key is unowned or already belongs to this attachment.
    godot::Error publish_join_presence(const godot::String &route);
    // Remove only the connect value written by this exact attachment.
    godot::Error clear_join_presence();
    // Withdraw the exact hosted lobby and stop accepting its members before leaving it.
    void retire_hosted_lobby();
    // Retire the exact account reservation and its live lobby view.
    void retire_membership();
    // Prepare the native client ticket before connecting to a certified direct server.
    void begin_direct_resolve(const godot::String &listing,
            const godot::Ref<OnlineDirectoryRequest> &request);
    // Start a fresh backend authorization for an exact hosted relay locator.
    void begin_hosted_resolve(const godot::String &listing,
            const godot::Ref<OnlineDirectoryRequest> &request);
    // Consume the selected signed relay ticket before preparing the Steam app ticket.
    void poll_hosted_resolution();
    // Abandon the exact hosted authorization, without consuming any late signed result.
    void retire_hosted_resolution();
    // Issue a Steam ticket for this selected certified authority before connecting.
    void prepare_client_ticket(const godot::String &listing, uint64_t authority,
            const godot::Ref<session::SessionEndpoint> &endpoint, const godot::Ref<OnlineDirectoryRequest> &request);
    // Retire the exact guest ticket when its pending or running Game leaves.
    void retire_client_ticket();
#endif

protected:
    // Report whether the attached Steam transport and directory can open.
    bool is_game_available() const override;
    // Reserve Steam lobby publication for listen Games.
    bool supports_host_mode(bool dedicated) const override;
    // Invalidate game-facing capabilities before retiring this attachment.
    void retire_attachment() override;
    // Preflight a Steam lobby locator before invoking normal directory resolution.
    bool accepts_listing(const godot::Ref<OnlineListing> &listing) const override;
#ifdef CRIMSON_WITH_STEAM
    // Stage and publish a lobby for the already mounted Steam authority.
    void begin_publish(const godot::Ref<session::SessionEndpoint> &endpoint,
            const godot::StringName &world, const godot::Ref<OnlineDirectoryRequest> &request) override;
    // Join the invited lobby and resolve its immutable authority route.
    void begin_resolve(const godot::String &listing_id,
            const godot::Ref<OnlineDirectoryRequest> &request) override;
    // Close and leave this scope's hosted lobby.
    void remove_listing(const godot::String &listing_id) override;
    // Leave membership owned by the current guest.
    void leave_listing(const godot::String &listing_id) override;
#endif
    // Internal directory has no additional script methods.
    static void _bind_methods();

public:
    // Initialize the typed binding before configuring any optional SDK service.
    void initialize_binding(steam::SteamClient *client, const godot::String &error, const godot::StringName &name_space);
#ifdef CRIMSON_WITH_STEAM
    // Share the selected transport, game scope, and provider-scoped lobby namespace.
    void configure(const std::shared_ptr<SteamGameScope> &scope);
#endif
    // Resolve the capability view and gateway captured by the container.
    godot::Ref<SteamScope> get_steam_scope() const;
    // Return the invite gateway for this exact attachment.
    godot::Ref<GameInvites> get_invites() const;
    // Report whether this exact game binding has active invite services.
    bool is_available() const;
    // Release Steam lobby ownership and the invite subscription.
    ~SteamOnlineDirectory() override;
    // Request the active host's invite dialog once the Steam overlay is ready.
    godot::Error open_invite_overlay();
    // Submit an invitation to one validated account from the current owned lobby.
    godot::Error invite_player(const godot::String &steam_id);
};

// Selects the native Steam identity, directory, and P2P transport for a Crimson game.
class SteamProvider : public GameServiceProvider {
    GDCLASS(SteamProvider, GameServiceProvider)

public:
    // Visibility of an authority's published Steam lobby.
    enum Visibility {
        PRIVATE, // Joinable by Steam invite.
        FRIENDS_ONLY, // Joinable by friends and invitees.
        PUBLIC, // Listed in compatible Steam searches.
        INVISIBLE, // Searchable without appearing in Steam friends visibility.
    };

private:
    // Optional expected AppID. Zero accepts the one selected by the Steam client.
    int64_t app_id = 0;
    // Steam virtual port used by the native host.
    int64_t virtual_port = 0;
    // Stable game namespace advertised in shared app and lobby metadata.
    godot::String lobby_namespace = "crimson";
    // Explicit app channels enabled for authenticated Game Steam Messages.
    godot::TypedArray<godot::StringName> message_channels;
    // Visibility selected for subsequent Game attachments.
    Visibility visibility = PRIVATE;
    // Prefer relay to direct ICE for Steam listen-host connections when both routes work.
    bool prefer_relay_routes = false;
    // Select the separate dedicated SteamGameServer pipe for this provider branch.
    godot::Ref<steam::SteamServer> dedicated_settings;
    // Trusted game coordinator selected by the game before boot.
    godot::Ref<SteamRelayCoordinator> relay_coordinator;
    // Optional authenticated commerce adapter captured at game boot.
    godot::Ref<steam::SteamCommerceBackend> commerce_backend;

    // Copy configuration without copying any attached Game state.
    godot::Ref<SteamProvider> branch() const;
    // Branch with the selected lobby visibility.
    godot::Ref<SteamProvider> with_visibility(Visibility value) const;

protected:
    // Build a detached Steam-aware Session while preserving the offline fallback.
    session::GameSession *create_session_for(GameRuntime &game) override;
    // Construct the directory for the same captured Steam services.
    godot::Ref<OnlineDirectory> create_online_directory_for(GameRuntime &game,
            session::GameSession &session) override;
    // Expose configuration, readiness, and explicit invitation actions to Godot.
    static void _bind_methods();

public:
    // Create a private provider with the Steam launch AppID.
    static godot::Ref<SteamProvider> configure();
    // Branch for the selected expected AppID.
    godot::Ref<SteamProvider> for_app(int64_t value) const;
    // Branch for an invite-only lobby.
    godot::Ref<SteamProvider> private_lobby() const;
    // Branch for a lobby visible and joinable through Steam friends.
    godot::Ref<SteamProvider> friends_only() const;
    // Branch for an openly searchable lobby.
    godot::Ref<SteamProvider> public_lobbies() const;
    // Branch for a searchable lobby absent from Steam friends visibility.
    godot::Ref<SteamProvider> invisible_lobby() const;
    // Branch for a bounded matchmaking namespace.
    godot::Ref<SteamProvider> with_lobby_namespace(const godot::String &value) const;
    // Branch for a chosen Steam virtual port.
    godot::Ref<SteamProvider> on_virtual_port(int64_t value) const;
    // Branch with a per-listener Steam relay preference and direct ICE fallback.
    godot::Ref<SteamProvider> prefer_relay() const;
    // Select route preference for the next Steam listen attachment.
    void set_prefer_relay_routes(bool enabled);
    // Read the current listen route preference.
    bool get_prefer_relay_routes() const;
    // Branch with authenticated Steam dedicated server settings.
    godot::Ref<SteamProvider> dedicated(const godot::Ref<steam::SteamServer> &settings) const;
    // Branch with the trusted backend for hosted server registrations and fresh client tickets.
    godot::Ref<SteamProvider> with_relay_coordinator(const godot::Ref<SteamRelayCoordinator> &coordinator) const;
    // Branch with an authenticated game-owned commerce adapter.
    godot::Ref<SteamProvider> with_commerce_backend(const godot::Ref<steam::SteamCommerceBackend> &backend) const;
    // Branch with distinct app channels for Game-bound Steam Messages.
    godot::Ref<SteamProvider> with_message_channels(const godot::TypedArray<godot::StringName> &channels) const;
    // Choose the next attachment's visibility in the Inspector.
    void set_lobby_visibility(Visibility value);
    // Read the configured next attachment's visibility.
    Visibility get_lobby_visibility() const;
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

VARIANT_ENUM_CAST(crimson::integration::SteamProvider::Visibility);
