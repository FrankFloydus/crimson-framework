// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#include "integration/game/game_network_diagnostics.hpp"

#include "integration/game/crimson_game.hpp"
#include "integration/game/game.hpp"

#include <godot_cpp/classes/e_net_multiplayer_peer.hpp>
#include <godot_cpp/classes/e_net_packet_peer.hpp>
#include <godot_cpp/core/class_db.hpp>

using namespace godot;

namespace crimson::integration {

void GameNetworkDiagnostics::_bind_methods() {
    ClassDB::bind_method(D_METHOD("get_role"), &GameNetworkDiagnostics::get_role);
    ClassDB::bind_method(D_METHOD("get_phase"), &GameNetworkDiagnostics::get_phase);
    ClassDB::bind_method(D_METHOD("get_transport"), &GameNetworkDiagnostics::get_transport);
    ClassDB::bind_method(D_METHOD("get_peer_id"), &GameNetworkDiagnostics::get_peer_id);
    ClassDB::bind_method(D_METHOD("get_player_number"), &GameNetworkDiagnostics::get_player_number);
    ClassDB::bind_method(D_METHOD("get_connected_peers"), &GameNetworkDiagnostics::get_connected_peers);
    ClassDB::bind_method(D_METHOD("get_round_trip_ms"), &GameNetworkDiagnostics::get_round_trip_ms);
    ClassDB::bind_method(D_METHOD("get_movement_mode"), &GameNetworkDiagnostics::get_movement_mode);
    ClassDB::bind_method(D_METHOD("get_simulation_tick"), &GameNetworkDiagnostics::get_simulation_tick);
    ClassDB::bind_method(D_METHOD("get_acknowledged_input"), &GameNetworkDiagnostics::get_acknowledged_input);
    ClassDB::bind_method(D_METHOD("get_pending_inputs"), &GameNetworkDiagnostics::get_pending_inputs);
    ClassDB::bind_method(D_METHOD("get_corrections"), &GameNetworkDiagnostics::get_corrections);
    ClassDB::bind_method(D_METHOD("get_correction_distance"), &GameNetworkDiagnostics::get_correction_distance);
    ClassDB::bind_method(D_METHOD("get_authority_position"), &GameNetworkDiagnostics::get_authority_position);
    ClassDB::bind_method(D_METHOD("get_predicted_position"), &GameNetworkDiagnostics::get_predicted_position);
    ClassDB::bind_method(D_METHOD("get_state_age_ms"), &GameNetworkDiagnostics::get_state_age_ms);
    ClassDB::bind_method(D_METHOD("get_replayed_inputs"), &GameNetworkDiagnostics::get_replayed_inputs);

    ADD_PROPERTY(PropertyInfo(Variant::INT, "role"), "", "get_role");
    ADD_PROPERTY(PropertyInfo(Variant::INT, "phase"), "", "get_phase");
    ADD_PROPERTY(PropertyInfo(Variant::STRING_NAME, "transport"), "", "get_transport");
    ADD_PROPERTY(PropertyInfo(Variant::INT, "peer_id"), "", "get_peer_id");
    ADD_PROPERTY(PropertyInfo(Variant::INT, "player_number"), "", "get_player_number");
    ADD_PROPERTY(PropertyInfo(Variant::INT, "connected_peers"), "", "get_connected_peers");
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "round_trip_ms"), "", "get_round_trip_ms");
    ADD_PROPERTY(PropertyInfo(Variant::INT, "movement_mode"), "", "get_movement_mode");
    ADD_PROPERTY(PropertyInfo(Variant::INT, "simulation_tick"), "", "get_simulation_tick");
    ADD_PROPERTY(PropertyInfo(Variant::INT, "acknowledged_input"), "", "get_acknowledged_input");
    ADD_PROPERTY(PropertyInfo(Variant::INT, "pending_inputs"), "", "get_pending_inputs");
    ADD_PROPERTY(PropertyInfo(Variant::INT, "corrections"), "", "get_corrections");
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "correction_distance"), "", "get_correction_distance");
    ADD_PROPERTY(PropertyInfo(Variant::VECTOR3, "authority_position"), "", "get_authority_position");
    ADD_PROPERTY(PropertyInfo(Variant::VECTOR3, "predicted_position"), "", "get_predicted_position");
    ADD_PROPERTY(PropertyInfo(Variant::INT, "state_age_ms"), "", "get_state_age_ms");
    ADD_PROPERTY(PropertyInfo(Variant::INT, "replayed_inputs"), "", "get_replayed_inputs");

    BIND_ENUM_CONSTANT(IDLE);
    BIND_ENUM_CONSTANT(OFFLINE);
    BIND_ENUM_CONSTANT(HOST);
    BIND_ENUM_CONSTANT(CONNECTING);
    BIND_ENUM_CONSTANT(GUEST);
    BIND_ENUM_CONSTANT(DEDICATED);
    BIND_ENUM_CONSTANT(MOVEMENT_DISABLED);
    BIND_ENUM_CONSTANT(AUTHORITY_LIVE);
    BIND_ENUM_CONSTANT(AUTHORITY_PREDICTIVE);
    BIND_ENUM_CONSTANT(GUEST_INTERPOLATED);
    BIND_ENUM_CONSTANT(GUEST_PREDICTED);
}

GameNetworkDiagnostics::Role GameNetworkDiagnostics::get_role() const { return role; }
int64_t GameNetworkDiagnostics::get_phase() const { return phase; }
StringName GameNetworkDiagnostics::get_transport() const { return transport; }
int64_t GameNetworkDiagnostics::get_peer_id() const { return peer_id; }
int64_t GameNetworkDiagnostics::get_player_number() const { return player_number; }
int64_t GameNetworkDiagnostics::get_connected_peers() const { return connected_peers; }
double GameNetworkDiagnostics::get_round_trip_ms() const { return round_trip_ms; }
GameNetworkDiagnostics::MovementMode GameNetworkDiagnostics::get_movement_mode() const { return movement_mode; }
int64_t GameNetworkDiagnostics::get_simulation_tick() const { return simulation_tick; }
int64_t GameNetworkDiagnostics::get_acknowledged_input() const { return acknowledged_input; }
int64_t GameNetworkDiagnostics::get_pending_inputs() const { return pending_inputs; }
int64_t GameNetworkDiagnostics::get_corrections() const { return corrections; }
double GameNetworkDiagnostics::get_correction_distance() const { return correction_distance; }
Vector3 GameNetworkDiagnostics::get_authority_position() const { return authority_position; }
Vector3 GameNetworkDiagnostics::get_predicted_position() const { return predicted_position; }
int64_t GameNetworkDiagnostics::get_state_age_ms() const { return state_age_ms; }
int64_t GameNetworkDiagnostics::get_replayed_inputs() const { return replayed_inputs; }

Ref<GameNetworkDiagnostics> Game::network_diagnostics() const {
    Ref<GameNetworkDiagnostics> result;
    result.instantiate();
    const GameRuntime *runtime = resolve_game();
    if (!runtime || !runtime->session_node) {
        return result;
    }
    auto *session = runtime->session_node;
    result->phase = runtime->get_phase();
    switch (session->get_state()) {
        case session::GameSession::OFFLINE:
            result->role = GameNetworkDiagnostics::OFFLINE;
            break;
        case session::GameSession::HOSTING:
            result->role = runtime->immediate_active && runtime->immediate_mode == GameRuntime::ImmediateMode::Dedicated ?
                    GameNetworkDiagnostics::DEDICATED : GameNetworkDiagnostics::HOST;
            break;
        case session::GameSession::CONNECTING:
            result->role = GameNetworkDiagnostics::CONNECTING;
            break;
        case session::GameSession::CLIENT:
            result->role = GameNetworkDiagnostics::GUEST;
            break;
        case session::GameSession::IDLE:
            return result;
    }
    result->peer_id = session->get_local_peer_id();
    const PackedInt32Array peers = session->get_connected_peer_ids();
    result->connected_peers = peers.size();
    if (result->role != GameNetworkDiagnostics::OFFLINE) {
        const Ref<ENetMultiplayerPeer> enet = session->get_multiplayer()->get_multiplayer_peer();
        result->transport = enet.is_valid() ? StringName("enet") : StringName("custom");
        if (enet.is_valid()) {
            for (int64_t index = 0; index < peers.size(); ++index) {
                const int32_t peer_id = peers[index];
                if (result->role == GameNetworkDiagnostics::GUEST && peer_id != 1) {
                    continue;
                }
                const Ref<ENetPacketPeer> peer = enet->get_peer(peer_id);
                if (peer.is_valid() && peer->get_state() == ENetPacketPeer::STATE_CONNECTED) {
                    result->round_trip_ms = MAX(result->round_trip_ms,
                            peer->get_statistic(ENetPacketPeer::PEER_ROUND_TRIP_TIME));
                }
            }
        }
    }
    if (runtime->immediate_active && runtime->immediate) {
        result->player_number = runtime->immediate->get_local_player_number();
        const auto motion = runtime->immediate->get_movement_diagnostics();
        using Mode = WorldSession::MovementDiagnostics::Mode;
        switch (motion.mode) {
            case Mode::DISABLED: break;
            case Mode::AUTHORITY_LIVE: result->movement_mode = GameNetworkDiagnostics::AUTHORITY_LIVE; break;
            case Mode::AUTHORITY_STATIC: result->movement_mode = GameNetworkDiagnostics::AUTHORITY_PREDICTIVE; break;
            case Mode::GUEST_INTERPOLATED: result->movement_mode = GameNetworkDiagnostics::GUEST_INTERPOLATED; break;
            case Mode::GUEST_PREDICTED: result->movement_mode = GameNetworkDiagnostics::GUEST_PREDICTED; break;
        }
        result->simulation_tick = motion.simulation_tick;
        result->authority_position = motion.authority_position;
        if (result->role == GameNetworkDiagnostics::GUEST) {
            result->acknowledged_input = motion.acknowledged_input;
            result->pending_inputs = motion.pending_inputs;
            result->state_age_ms = motion.state_age_ms;
            if (result->movement_mode == GameNetworkDiagnostics::GUEST_PREDICTED) {
                result->corrections = motion.corrections;
                result->correction_distance = motion.correction_distance;
                result->predicted_position = motion.predicted_position;
                result->replayed_inputs = motion.replayed_inputs;
            }
        }
    }
    return result;
}

} // namespace crimson::integration
