// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#pragma once

#include "modules/steam/client/steam_voice.hpp"

#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/core/binder_common.hpp>
#include <godot_cpp/variant/callable.hpp>

#include <deque>
#include <map>
#include <set>

namespace crimson::integration {

class GameRuntime;

// Authority-routed, opt-in voice for one persistent GameRuntime.
class GameVoice : public godot::Node {
    GDCLASS(GameVoice, godot::Node)

public:
    // Channel selected by a speaking player and approved by authority policy.
    enum Channel {
        GROUP, // All eligible participants in the current game.
        TEAM, // Only recipients selected by the authority's team policy.
        PROXIMITY, // Only recipients selected by the authority's proximity policy.
    };

    // Begin explicit local push-to-talk when the player has reached this world.
    godot::Error start_talking();
    // Request native capture stop and immediately stop network transmission.
    godot::Error stop_talking();
    // Mute this peer locally and discard its queued playback.
    godot::Error set_muted(int64_t peer_id, bool muted = true);
    // Choose the outgoing channel to submit with future frames.
    godot::Error set_channel(Channel channel);
    // Supply an authority predicate (sender_peer_id, recipient_peer_id, channel) -> bool.
    godot::Error set_recipient_policy(const godot::Callable &policy);

    // Configure matching voice RPCs on this persistent path.
    void _enter_tree() override;
    // Advance capture, bounded playback and activity state.
    void _process(double delta) override;
    // Invalidate the playback queue and stop any held microphone.
    void _exit_tree() override;

protected:
    // Register public push-to-talk methods, observation signals and wire methods.
    static void _bind_methods();

private:
    // A sender's bounded rate window and latest accepted sequence.
    struct SenderWindow {
        // Epoch-relative sequence most recently accepted.
        int64_t latest = 0;
        // Fixed monotonic interval start.
        uint64_t started = 0;
        // Number of frames accepted during the interval.
        uint32_t packets = 0;
        // Encoded bytes accepted during the interval.
        uint32_t bytes = 0;
    };

    // Detached bounded frame waiting for this client's decode budget.
    struct Incoming {
        // Authority-attributed local peer ID.
        int32_t sender;
        // Owned encoded bytes.
        godot::PackedByteArray bytes;
        // Monotonic enqueue timestamp used to discard stale playback.
        uint64_t received_at;
    };

    // Resolve the owning live game while its persistent scope is active.
    GameRuntime *game() const;
    // Require the authority's exact Steam-authenticated, world-ready participant.
    bool authority_peer(const GameRuntime &owner, int32_t peer) const;
    // Clear transient state on departure or committed epoch change.
    void reset(bool notify);
    // Remove departed participants and expire inferred remote activity.
    void expire_peers(GameRuntime &owner, uint64_t now);
    // Capture or route one frame from the explicitly talking local player.
    void send_local(GameRuntime &owner);
    // Receive an unreliable game voice frame from an actual remote peer.
    void rpc_submit(int64_t epoch, int64_t sequence, int64_t channel, const godot::PackedByteArray &bytes);
    // Receive a frame attributed by the current authority, never by another client.
    void rpc_receive(int64_t epoch, int64_t sender, int64_t sequence, const godot::PackedByteArray &bytes);
    // Validate one frame and compute its recipients on the authority.
    void relay(GameRuntime &owner, int32_t sender, int64_t sequence, Channel channel,
            const godot::PackedByteArray &bytes);
    // Append an authorized frame under local mute and playback bounds.
    void enqueue(int32_t sender, int64_t sequence, const godot::PackedByteArray &bytes);
    // Decode a small bounded batch and emit audio attributed to each peer.
    void play_frames(GameRuntime &owner, uint64_t now);

    // Exact committed epoch corresponding to all active queue and rate state.
    int64_t voice_epoch = 0;
    // Monotonic encoded sequence for this local role in the current world.
    int64_t local_sequence = 0;
    // Current local push-to-talk channel.
    Channel outgoing_channel = GROUP;
    // Trusted game-owned visibility policy. An empty predicate enables GROUP only.
    godot::Callable recipient_policy;
    // Capturing codec handle held only while explicitly talking in the current world.
    godot::Ref<steam::SteamVoiceCapture> capture;
    // Exact local peer whose talk state was published with this capture.
    int32_t local_talker = 0;
    // Admitted peer rate and replay windows on the authority.
    std::map<int32_t, SenderWindow> senders;
    // Last relay sequence accepted for each attributed sender on a receiving client.
    std::map<int32_t, int64_t> received;
    // Locally muted peer IDs for this exact epoch.
    std::set<int32_t> muted_peers;
    // Remote talk state inferred from successfully decoded frames.
    std::map<int32_t, uint64_t> last_audio;
    // Bounded incoming playback queue.
    std::deque<Incoming> incoming;
    // Aggregate fan-out accounting interval.
    uint64_t relay_window = 0;
    // Compressed bytes sent to recipients during this interval.
    uint32_t relayed_bytes = 0;
    // Bound trusted recipient predicate calls under concurrent incoming audio.
    uint32_t recipient_checks = 0;
    // Do not expose gameplay actions after the node starts tree teardown.
    bool leaving_tree = false;
};

} // namespace crimson::integration

VARIANT_ENUM_CAST(crimson::integration::GameVoice::Channel);
