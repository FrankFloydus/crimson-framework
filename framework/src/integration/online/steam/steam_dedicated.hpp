// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#pragma once

#include "integration/online/online_directory.hpp"
#include "modules/steam/server/steam_server.hpp"

#include <godot_cpp/classes/node.hpp>

#include <memory>

namespace crimson::steam {
class SteamInventoryVerifier;
class SteamServerPlayerStats;
class SteamServerProgression;
class SteamServerRuntime;
}

namespace crimson::session {
class GameSession;
}

namespace crimson::integration {

class SteamProvider;
class SteamDedicatedAuthenticator;
class SteamDedicatedOnlineDirectory;
class SteamSessionTransport;
class SteamRelayCoordinator;
class SteamRelayRequest;

// Detached observation of a dedicated Game's Steam publication and admission.
class SteamServerStatus : public godot::RefCounted {
    GDCLASS(SteamServerStatus, godot::RefCounted)

public:
    // Report that the exact playerless Game is running with secure admission open.
    bool is_ready() const;
    // Report that this world requested explicit Steam advertisement.
    bool is_listed() const;
    // Report the local advertisement request, independently of Steam server browser readback.
    bool is_advertisement_requested() const;
    // Report live Steam server identity and certificate availability.
    bool is_secure() const;
    // Return the count of Session-admitted authenticated remote players.
    int32_t get_authenticated_players() const;
    // Return the published world ID or an empty name while unlisted and unready.
    godot::StringName get_world_id() const;

protected:
    // Register immutable status getters and their read-only properties.
    static void _bind_methods();

private:
    friend class SteamDedicatedServer;
    friend class SteamDedicatedOnlineDirectory;

    // Native Game has entered its normal running phase.
    bool ready = false;
    // Native discovery request was sent for this active world.
    bool listed = false;
    // Steam logged in with a current transport certificate.
    bool secure = false;
    // Number of verified and admitted remote peers at snapshot time.
    int32_t authenticated_players = 0;
    // Published stable Game world identity.
    godot::StringName world_id;
};

// Observation facade for one exact Steam dedicated Game attachment.
class SteamDedicatedServer : public godot::RefCounted {
    GDCLASS(SteamDedicatedServer, godot::RefCounted)

public:
    // Return the exact current detached status or the idle snapshot after retirement.
    godot::Ref<SteamServerStatus> status() const;
    // Return the safe startup diagnostic for an unavailable dedicated attachment.
    godot::String get_initialization_error() const;
    // Return this exact attachment's authenticated player inventory verifier.
    godot::Ref<steam::SteamInventoryVerifier> inventory_verifier() const;
    // Resolve Steam progression for an authenticated remote peer of this dedicated game.
    godot::Ref<steam::SteamServerPlayerStats> stats(int64_t peer_id) const;

protected:
    // Register status observation and safe startup diagnostics.
    static void _bind_methods();

private:
    friend class SteamDedicatedOnlineDirectory;

    // Weak exact directory for live state.
    godot::ObjectID directory_id;
    // Last status published to observers.
    godot::Ref<SteamServerStatus> previous;
    // Safe diagnostic from the selected GameServer runtime.
    godot::String initialization_error;
    // Owned verifier invalidated with this exact dedicated directory.
    godot::Ref<steam::SteamInventoryVerifier> inventory_service;

    // Emit only when public state changed, without retaining the Game.
    void notify();
};

// Persistent native service under the Game's Session, including failed SDK setup.
class SteamDedicatedRuntimeNode : public godot::Node {
    GDCLASS(SteamDedicatedRuntimeNode, godot::Node)

public:
    // Configure the detached Session for dedicated Steam without acquiring a client pipe.
    static session::GameSession *create_session(GameRuntime &game, const SteamProvider &provider,
            const godot::Ref<steam::SteamServer> &settings, uint32_t app_id, const godot::StringName &name_space,
            int32_t virtual_port, const godot::Ref<SteamRelayCoordinator> &coordinator);
    // Create the directory for this exact persistent Session and Game.
    static godot::Ref<OnlineDirectory> create_directory(GameRuntime &game, const SteamProvider &provider,
            session::GameSession &session);
    // Retire the persistent validator when this exact Session is destroyed.
    ~SteamDedicatedRuntimeNode() override;
    // Pump dedicated Steam callbacks and advance exact ticket and advertisement work.
    void _process(double delta) override;

protected:
    // Register the internal dedicated service node.
    static void _bind_methods();

private:
    friend class SteamDedicatedOnlineDirectory;

    // Persistent Game that selected this dedicated configuration.
    godot::ObjectID game_id;
    // Provider Resource that owns this persistent attachment.
    godot::ObjectID provider_id;
    // The actual GameSession tracking admitted peers for this node.
    godot::ObjectID session_id;
    // Current directory for the live tree attachment.
    godot::ObjectID directory_id;
    // Settings pinned at the persistent Session's creation.
    godot::Ref<steam::SteamServer> settings;
    // Matchmaking namespace validated before Steam starts.
    godot::StringName name_space;
    // Hosted listener's SDK virtual port.
    int32_t virtual_port = 0;
    // Trusted backend for this exact hosted deployment.
    godot::Ref<SteamRelayCoordinator> relay_coordinator;
    // Diagnostic from SDK-off, missing runtime or invalid options.
    godot::String startup_error;
#ifdef CRIMSON_WITH_STEAM
    // Exact dedicated Steam runtime with separate native callback pipe.
    std::shared_ptr<steam::SteamServerRuntime> runtime;
    // Preserve issued store observations across dedicated Game attachment changes on this pipe.
    std::shared_ptr<steam::SteamServerProgression> progression;
    // Native transport configured for the dedicated socket.
    godot::Ref<SteamSessionTransport> transport;
    // Native ticket validator that drains per-account pending attempts.
    godot::ObjectID auth_id;
#endif
};

// Publishes and withdraws one authenticated playerless Game on the dedicated pipe.
class SteamDedicatedOnlineDirectory : public OnlineDirectory {
    GDCLASS(SteamDedicatedOnlineDirectory, OnlineDirectory)

public:
    // Resolve the persistent node and attach a new generation of dedicated observations.
    void attach(SteamDedicatedRuntimeNode *node, const godot::String &error);
    // Advance login and publication on the native scene-thread pump.
    void advance();
    // Read the scoped dedicated status facade.
    godot::Ref<SteamDedicatedServer> service() const;
    // Resolve the player's exact current Session admission and Steam-certified account.
    godot::Ref<steam::SteamServerPlayerStats> player_stats(int32_t peer_id) const;
    // Snapshot exact platform and Game state on the scene thread.
    godot::Ref<SteamServerStatus> status() const;
    // Withdraw owned advertisement and invalidate retained observers before release.
    ~SteamDedicatedOnlineDirectory() override;

protected:
    // Register the internal native dedicated directory.
    static void _bind_methods();
    // Allow startup only when native configuration and the dedicated pipe exist.
    bool is_game_available() const override;
    // Dedicated provider accepts only the explicit dedicated hosting action.
    bool supports_host_mode(bool dedicated) const override;
    // Stage publication until Steam login, certificate, listener and world are ready.
    void begin_publish(const godot::Ref<session::SessionEndpoint> &endpoint, const godot::StringName &world,
            const godot::Ref<OnlineDirectoryRequest> &request) override;
    // A headless dedicated provider never resolves a guest route.
    void begin_resolve(const godot::String &listing, const godot::Ref<OnlineDirectoryRequest> &request) override;
    // Withdraw only the route published by the exact active dedicated world.
    void remove_listing(const godot::String &listing) override;
    // End this exact tree attachment and stop emitting Game-bound status.
    void retire_attachment() override;

private:
    // Weak exact persistent node that owns the dedicated SDK runtime.
    godot::ObjectID node_id;
    // Safe public observations for this exact tree attachment.
    godot::Ref<SteamDedicatedServer> status_service;
    // Pending publication awaiting Steam's ready state.
    godot::Ref<OnlineDirectoryRequest> pending;
    // Exact hosted registration awaiting the trusted backend.
    godot::Ref<SteamRelayRequest> relay_registration;
    // Requested stable world for the pending or committed publication.
    godot::StringName world_id;
    // Exact committed direct listing identifier for the current world.
    godot::String listing_id;
    // Certificate identity pinned to the listing at publication.
    uint64_t published_server_id = 0;
    // Operator override for the advertised direct IPv4, zero uses runtime selection.
    uint32_t public_ipv4 = 0;
    // Block use after this directory's Game leaves the tree.
    bool retired = false;

    // Resolve only the exact node still associated with this attachment.
    SteamDedicatedRuntimeNode *resolve_node() const;
    // Find the admitted identity for this Steam account, optionally restricted to one connection.
    godot::ObjectID authenticated_account(uint64_t account, int32_t peer_id = 0) const;
#ifdef CRIMSON_WITH_STEAM
    // Complete direct publication when a certified IPv4 route is available.
    void advance_direct_publication(SteamDedicatedRuntimeNode &node);
    // Obtain and acknowledge exact coordinator registration before opening hosted admission.
    void advance_hosted_publication(SteamDedicatedRuntimeNode &node);
#endif
};

} // namespace crimson::integration
