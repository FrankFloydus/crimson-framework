// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#pragma once

#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/variant/string_name.hpp>
#include <godot_cpp/variant/vector3.hpp>

#include <cstdint>

namespace crimson::integration {

// Detached local observation of a game's connection and configured player movement.
class GameNetworkDiagnostics : public godot::RefCounted {
    GDCLASS(GameNetworkDiagnostics, godot::RefCounted)

    friend class Game;

public:
    // Distinguish transport admission and authority roles without exposing a mutable peer.
    enum Role { IDLE, OFFLINE, HOST, CONNECTING, GUEST, DEDICATED };
    // Describe the movement pipeline observed at the local game scope.
    enum MovementMode { MOVEMENT_DISABLED, AUTHORITY_LIVE, AUTHORITY_PREDICTIVE, GUEST_INTERPOLATED, GUEST_PREDICTED };

private:
    Role role = IDLE; ///< Local session role, or IDLE for a retired scope.
    int64_t phase = 0; ///< Game.Phase at sampling time.
    godot::StringName transport; ///< ENet, a custom peer, or empty when offline.
    int64_t peer_id = 0; ///< Local connection ID, or zero before admission.
    int64_t player_number = 0; ///< One-based local spawn slot, or zero without a fixed-world actor.
    int64_t connected_peers = 0; ///< Count of admitted remote peers known to the local session.
    double round_trip_ms = -1.0; ///< ENet's mean RTT for the host link or highest guest link, in milliseconds.
    MovementMode movement_mode = MOVEMENT_DISABLED; ///< Local fixed-world movement pipeline.
    int64_t simulation_tick = 0; ///< Latest completed authority simulation tick observed locally.
    int64_t acknowledged_input = 0; ///< Owner input sequence accounted for by the latest authoritative state.
    int64_t pending_inputs = 0; ///< Local movement commands awaiting confirmation.
    int64_t corrections = 0; ///< Count of visible-size owner corrections in this session.
    double correction_distance = 0.0; ///< Most recent owner correction distance, in metres.
    godot::Vector3 authority_position; ///< Latest authoritative owner position when available.
    godot::Vector3 predicted_position; ///< Current owner position after prediction and reconciliation.
    int64_t state_age_ms = -1; ///< Local age of the latest received owner state, or -1 when unavailable.
    int64_t replayed_inputs = 0; ///< Commands replayed when the latest authoritative state was reconciled.

protected:
    // Register read-only properties, their getters, and the role and movement enums.
    static void _bind_methods();

public:
    // Read the local transport role at sampling time.
    Role get_role() const;
    // Read the game's readiness phase at sampling time.
    int64_t get_phase() const;
    // Identify the underlying transport where it is available.
    godot::StringName get_transport() const;
    // Read the local admitted peer ID, or zero if there is none.
    int64_t get_peer_id() const;
    // Read the local actor's one-based fixed-world slot, or zero when unavailable.
    int64_t get_player_number() const;
    // Count admitted remote peers visible to this game scope.
    int64_t get_connected_peers() const;
    // Read the ENet round-trip estimate, or -1 for an unavailable measurement.
    double get_round_trip_ms() const;
    // Read the local movement path in use.
    MovementMode get_movement_mode() const;
    // Read the last observed server simulation tick.
    int64_t get_simulation_tick() const;
    // Read the last owner command acknowledged by authoritative state.
    int64_t get_acknowledged_input() const;
    // Count unacknowledged owner commands in the local history.
    int64_t get_pending_inputs() const;
    // Count owner movement corrections during the active session.
    int64_t get_corrections() const;
    // Read the most recent owner correction in metres.
    double get_correction_distance() const;
    // Read the latest authoritative position of the local actor when available.
    godot::Vector3 get_authority_position() const;
    // Read the current predicted position of the local actor when available.
    godot::Vector3 get_predicted_position() const;
    // Read the elapsed local milliseconds since receipt of the latest owner state, or -1 when unavailable.
    int64_t get_state_age_ms() const;
    // Count commands replayed to reconcile the latest authoritative state.
    int64_t get_replayed_inputs() const;
};

} // namespace crimson::integration

VARIANT_ENUM_CAST(crimson::integration::GameNetworkDiagnostics::Role);
VARIANT_ENUM_CAST(crimson::integration::GameNetworkDiagnostics::MovementMode);
