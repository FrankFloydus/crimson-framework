// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#pragma once

#include "modules/steam/client/steam_delivery.hpp"
#include "modules/steam/client/steam_result.hpp"

#include <godot_cpp/core/object_id.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>
#include <godot_cpp/variant/string_name.hpp>
#include <godot_cpp/variant/typed_array.hpp>

#include <array>
#include <deque>
#include <functional>
#include <map>
#include <memory>

namespace crimson::steam {

class SteamRuntime;
struct SteamClientScope;
class SteamMessages;

// Fixed-size native binding or challenge nonce carried only in the framework envelope.
using SteamMessageNonce = std::array<uint8_t, 16>;

// Validate a bounded list of distinct app channel names before authorizing a peer.
bool valid_steam_message_channels(const godot::TypedArray<godot::StringName> &channels, bool allow_empty = false);

// Authority-confirmed identity and committed world for a Game's actual connection.
struct SteamMessageGamePeer {
    // Canonical SteamID64 from this connection's admitted identity.
    godot::String steam_id;
    // The world epoch current on both peers.
    int64_t epoch = 0;
    // Exact admission object, replaced even if the same account returns to this world.
    godot::ObjectID admission;
};

// Scoped permission to send and receive Steam Messages on explicit app channels.
class SteamMessagePeer : public godot::RefCounted {
    GDCLASS(SteamMessagePeer, godot::RefCounted)

public:
    // Queue one bounded frame on a permitted logical channel.
    godot::Error send(const godot::StringName &channel, const godot::PackedByteArray &bytes,
            SteamDelivery::Mode delivery = SteamDelivery::RELIABLE);
    // Release this exact binding and its native Messages channel.
    godot::Error close();

protected:
    // Register scoped sends and the typed observation signals.
    static void _bind_methods();

private:
    friend class SteamMessages;

    // Independent sequence lanes avoid suppressing reliable messages after an unreliable frame.
    struct Channel {
        // Outgoing sequence for each delivery mode.
        std::array<uint64_t, 2> sent{};
        // Latest accepted sequence for each delivery mode.
        std::array<uint64_t, 2> received{};
        // Local time of the latest accepted sequence, initialized by binding proof.
        std::array<uint64_t, 2> received_at{};
    };

    // Fixed per-peer bandwidth window.
    struct Budget {
        // Monotonic window start.
        uint64_t started = 0;
        // Number of frames in the current window.
        uint32_t packets = 0;
        // Encoded bytes in the current window.
        uint32_t bytes = 0;
    };

    // One reliable app frame waiting for the initial exact binding proof.
    struct Pending {
        // Authorized app channel.
        godot::StringName channel;
        // Owned bounded payload.
        godot::PackedByteArray bytes;
    };

    // Resolve only the facade that installed this binding.
    godot::Ref<SteamMessages> service() const;

    // Exact owning SteamMessages facade.
    godot::ObjectID service_id;
    // SDK-authenticated remote individual account.
    uint64_t account = 0;
    // Game peer ID when authorization comes from a native session, zero for standalone.
    int64_t game_peer = 0;
    // Committed Game world epoch, zero for standalone.
    int64_t epoch = 0;
    // Exact admission instance when this peer is Game-owned.
    godot::ObjectID admission;
    // Steam-clock watermark rejecting packets queued before this exact binding.
    int64_t created_at_usec = 0;
    // Permitted app names and independent reliable/unreliable sequences.
    std::map<godot::StringName, Channel> channels;
    // Scoped outbound rate limit.
    Budget outgoing;
    // Scoped inbound rate limit.
    Budget incoming;
    // Fresh local binding identity, required as the destination of every data frame.
    SteamMessageNonce local_nonce{};
    // Remote binding identity proven under the current challenge.
    SteamMessageNonce remote_nonce{};
    // Claimed remote binding being challenged, or all zero for initial discovery.
    SteamMessageNonce candidate_nonce{};
    // Fresh challenge a remote proof must echo for the current attempt.
    SteamMessageNonce challenge{};
    // Expiry of the current challenge.
    uint64_t challenge_deadline = 0;
    // Limit reliable challenge requests to a bounded cadence.
    uint64_t next_hello = 0;
    // Limit replies to peer challenge frames.
    uint64_t next_proof = 0;
    // Reliable app payloads queued while the peer binding is being proven.
    std::deque<Pending> pending;
    // Total bytes in the pre-handshake reliable queue.
    int64_t pending_bytes = 0;
    // Absolute deadline for the oldest queued reliable frame.
    uint64_t pending_deadline = 0;
};

// Outcome of authorizing one Steam Messages endpoint.
class SteamMessagePeerResult : public SteamResult {
    GDCLASS(SteamMessagePeerResult, SteamResult)

public:
    // Return the exact scoped permission after successful authorization.
    godot::Ref<SteamMessagePeer> get_value() const;

protected:
    // Register the typed result payload.
    static void _bind_methods();

private:
    friend class SteamMessages;

    // Make a completed authorization result.
    static godot::Ref<SteamMessagePeerResult> create(godot::Error error,
            const godot::Ref<SteamMessagePeer> &peer = {});

    // Scoped peer on success.
    godot::Ref<SteamMessagePeer> value;
};

// Exact standalone allowlist or Game-certified peer Messages routing.
class SteamMessages : public godot::RefCounted {
    GDCLASS(SteamMessages, godot::RefCounted)

public:
    // Authorize one admitted Game peer using configured app channels.
    godot::Ref<SteamMessagePeerResult> for_peer(int64_t peer_id);
    // Authorize one exact Steam account for explicit standalone app channels.
    godot::Ref<SteamMessagePeerResult> authorize(const godot::String &steam_id,
            const godot::TypedArray<godot::StringName> &channels);

    // Configure a Game-backed scope with native admission and epoch checks.
    void set_game_source(std::function<SteamMessageGamePeer(int64_t)> source, const godot::StringName &name_space,
            const godot::TypedArray<godot::StringName> &channels);

protected:
    // Register the typed authorization surface.
    static void _bind_methods();

private:
    friend class SteamClient;
    friend class SteamMessagePeer;

    // Observe a client generation without enabling any native channel before authorization.
    void attach(const std::shared_ptr<SteamRuntime> &active, const std::shared_ptr<SteamClientScope> &scope);
    // Invalidate all exact owned bindings and release scoped callbacks.
    void detach();
    // Retire stale Game bindings and drain a single bounded process-wide native batch.
    void tick();
    // Resolve SDK access only on the scene thread and exact start.
    std::shared_ptr<SteamRuntime> active_runtime() const;
    // Verify the binding against the exact current scope and Game identity if applicable.
    bool owns_peer(const SteamMessagePeer *peer) const;
    // Acquire the native routing lease for this exact remote account.
    godot::Ref<SteamMessagePeerResult> bind_peer(uint64_t account, int64_t game_peer, int64_t epoch,
            godot::ObjectID admission, const godot::TypedArray<godot::StringName> &channels);
    // Check the exact scope and channel again before a send that implicitly accepts Steam traffic.
    godot::Error send_to(SteamMessagePeer *peer, const godot::StringName &channel,
            const godot::PackedByteArray &bytes, SteamDelivery::Mode delivery);
    // Submit one framed message after the bilateral binding is proven.
    godot::Error transmit(const std::shared_ptr<SteamRuntime> &active, SteamMessagePeer &peer,
            const godot::StringName &channel, const godot::PackedByteArray &bytes, SteamDelivery::Mode mode);
    // Initiate or retry one bounded reliable freshness challenge.
    void request_proof(const std::shared_ptr<SteamRuntime> &active, SteamMessagePeer &peer,
            const SteamMessageNonce &candidate, uint64_t now);
    // Process a copied nonce and challenge from a Steam-authenticated control packet.
    void handle_proof(const std::shared_ptr<SteamRuntime> &active, SteamMessagePeer &peer, uint8_t kind,
            const SteamMessageNonce &source, const SteamMessageNonce &target, const SteamMessageNonce &challenge,
            uint64_t now);
    // Flush already budgeted reliable data or continue negotiating before its deadline.
    void flush_peer(const std::shared_ptr<SteamRuntime> &active, const godot::Ref<SteamMessagePeer> &peer, uint64_t now);
    // Retire the owned channel before notifying the caller.
    godot::Error close_peer(const SteamMessagePeer *peer, godot::Error reason);
    // Accept a native session request only for an already allowed endpoint.
    void on_session_request(const void *data, size_t size);
    // Spend one bounded per-peer bandwidth allocation.
    static bool spend_budget(SteamMessagePeer::Budget &budget, uint32_t bytes, uint64_t now);

    // Client SDK borrowed by this service.
    std::weak_ptr<SteamRuntime> runtime;
    // Exact scope lifetime that invalidates all retained peers.
    std::weak_ptr<SteamClientScope> client_scope;
    // Native Game validator, absent on standalone clients.
    std::function<SteamMessageGamePeer(int64_t)> game_source;
    // Matching namespace encoded in Game message frames.
    godot::StringName game_namespace;
    // Game-configured logical channels.
    godot::TypedArray<godot::StringName> game_channels;
    // Owned bindings by exact SDK identity.
    std::map<uint64_t, godot::Ref<SteamMessagePeer>> peers;
    // Scoped Steam incoming session callback subscription.
    uint64_t request_subscription = 0;
};

} // namespace crimson::steam
