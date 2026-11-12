// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#pragma once

#include "integration/game/game_services.hpp"

#include <memory>

namespace crimson::integration {

#ifdef CRIMSON_WITH_EOS
struct EosOnlineState;

// Pumps one developer login and its platform on the owning Godot scene thread.
class CrimsonEOSPoller : public godot::Node {
    GDCLASS(CrimsonEOSPoller, godot::Node)
    friend class CrimsonEOSProvider;

    // Scoped SDK state shared with the provider's native services.
    std::shared_ptr<EosOnlineState> state;

protected:
    // Register the internal node type without exposing runtime state.
    static void _bind_methods();

public:
    // Tick EOS and bound the pending sign-in lifetime.
    void _process(double delta) override;
    // Cancel login and retire owned listings when the bootstrap owner leaves.
    void _exit_tree() override;
};

// Verifies the Connect ID token against the actual EOS transport identity.
class CrimsonEOSAuthenticator : public session::CrimsonSessionAuthenticator {
    GDCLASS(CrimsonEOSAuthenticator, session::CrimsonSessionAuthenticator)
    friend class CrimsonEOSProvider;

    // Logged-in SDK platform and its current EOS transport.
    std::shared_ptr<EosOnlineState> state;

protected:
    // Register the internal authenticator type without exposing credentials.
    static void _bind_methods();
    // Answer the session challenge with the local Connect identity token.
    godot::PackedByteArray make_proof(int32_t peer_id, bool authority,
            const godot::PackedByteArray &challenge) override;
    // Verify the token for the PUID bound to the actual pending EOS connection.
    void verify(const godot::Ref<session::CrimsonSessionAuthRequest> &request) override;
};

// Publishes and resolves an EOS Lobby containing the authority's P2P socket.
class CrimsonEOSLobbyDirectory : public CrimsonOnlineDirectory {
    GDCLASS(CrimsonEOSLobbyDirectory, CrimsonOnlineDirectory)
    friend class CrimsonEOSProvider;

    // Signed-in platform that owns the listing operations.
    std::shared_ptr<EosOnlineState> state;

protected:
    // Register the internal directory type without exposing SDK handles.
    static void _bind_methods();
    // Create the lobby and publish the live host's socket before completing the request.
    void begin_publish(const godot::Ref<session::CrimsonSessionEndpoint> &endpoint,
            const godot::StringName &world, const godot::Ref<CrimsonOnlineDirectoryRequest> &request) override;
    // Resolve the EOS lobby owner and published socket into the native transport route.
    void begin_resolve(const godot::String &listing_id,
            const godot::Ref<CrimsonOnlineDirectoryRequest> &request) override;
    // Destroy a listing owned by this signed-in platform.
    void remove_listing(const godot::String &listing_id) override;
    // Leave a lobby when the client retires the corresponding game session.
    void leave_listing(const godot::String &listing_id) override;
};
#endif

// First-party EOS provider for one developer-signed-in listen host or joining game.
class CrimsonEOSProvider : public CrimsonServiceProvider {
    GDCLASS(CrimsonEOSProvider, CrimsonServiceProvider)

    // Product configured in the Epic Developer Portal.
    godot::String product;
    // Sandbox that contains the selected deployment.
    godot::String sandbox;
    // Deployment used by both peers.
    godot::String deployment;
    // EOS client policy identifier.
    godot::String client;
    // Client policy secret held in memory and excluded from resource serialization.
    godot::String secret;
    // Use only the Epic relay for P2P transport when enabled.
    bool relay_only = false;

#ifdef CRIMSON_WITH_EOS
    // One scene-owned sign-in and game session per configured provider instance.
    std::shared_ptr<EosOnlineState> state;
#endif

protected:
    // Register fluent setup, developer sign-in, and status.
    static void _bind_methods();
    // Inject the native EOS transport and authenticator after Connect sign-in.
    session::CrimsonGameSession *create_session() override;
    // Inject the lobby directory sharing the session's authenticated platform.
    godot::Ref<CrimsonOnlineDirectory> create_online_directory() override;

public:
    // Configure the private EOS product without storing credentials in an Inspector resource.
    static godot::Ref<CrimsonEOSProvider> configure(const godot::String &product_id,
            const godot::String &sandbox_id, const godot::String &deployment_id,
            const godot::String &client_id, const godot::String &client_secret,
            bool force_relay = false);
    // Sign in with a named Developer Authentication Tool credential under a persistent owner.
    godot::Ref<CrimsonGameOperation> login_developer(godot::Node *owner,
            const godot::String &address, const godot::String &credential_name);
    // Report whether a usable Connect identity is bound to the live owner.
    bool is_ready() const;
    // Read the local Product User ID after successful Connect sign-in.
    godot::String get_product_user_id() const;
};

} // namespace crimson::integration
