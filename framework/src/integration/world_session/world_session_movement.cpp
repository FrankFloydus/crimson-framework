// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#include "integration/world_session/world_session.hpp"
#include "integration/player_control/player_control.hpp"

#include <godot_cpp/classes/character_body3d.hpp>
#include <godot_cpp/classes/e_net_multiplayer_peer.hpp>
#include <godot_cpp/classes/e_net_packet_peer.hpp>
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/time.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/core/math.hpp>

#include <array>
#include <cmath>
#include <limits>
#include <utility>

using namespace godot;

namespace crimson::integration {
namespace {

// The world session reuses its established reliable admission lane for the initial poses.
// The admitted session version selects the byte layout. Keep Godot's RPC names stable through admission.
constexpr const char *motion_roster_method = "_crimson_world_motion_roster_v2";
// Input contains intent only. Pose samples are written exclusively by the authority.
constexpr const char *motion_input_method = "_crimson_world_motion_input_v2";
constexpr const char *motion_pose_method = "_crimson_world_motion_pose_v1";
constexpr const char *owner_correction_method = "_crimson_world_motion_owner_v1";
constexpr int32_t authority_peer = 1;
// 30 Hz local uploads with tolerance for jitter and batched network polling.
constexpr uint64_t upload_interval_ms = 32;
constexpr uint64_t minimum_input_interval_ms = 8;
// Recent commands overlap on the wire to recover isolated losses and short bursts.
constexpr int64_t command_redundancy = 24;
constexpr int64_t input_window = 96;
constexpr size_t owner_history_limit = 120;
constexpr size_t maximum_replay_steps = 64;
constexpr int32_t max_prediction_tick_rate = 120;
// Begin consuming the remote stream after a short cushion instead of on arrival.
constexpr uint64_t input_start_cushion_ms = 45;
// Held remote steering expires without fresh packets, even if the last packet was nonzero.
constexpr uint64_t input_lifetime_ms = 300;
// Full 20 Hz authority samples keep state repair independent of earlier packets.
constexpr double snapshot_interval = 0.05;
// Bound each high-rate packet below typical path MTU before Godot/transport overhead.
constexpr int64_t snapshot_batch_size = 24;
// Aim is expressed in world yaw and pitch. Reject out-of-range or non-finite values.
constexpr double pitch_limit = 1.553343;
constexpr double correction_threshold = 0.005;
constexpr double correction_snap_distance = 0.35;

// Skip resimulation when both peers produced the same state for this exact input frame.
bool same_step_state(const mover::PredictionState &first, const mover::PredictionState &second,
        double first_yaw, double second_yaw) {
    return first.position.is_equal_approx(second.position) && first.velocity.is_equal_approx(second.velocity) &&
            first.grounded == second.grounded &&
            Math::is_zero_approx(Math::angle_difference(first_yaw, second_yaw));
}

// Resolve an actor or view without extending the node's lifetime.
Node3D *find_motion_node(uint64_t id) {
    return id ? Object::cast_to<Node3D>(ObjectDB::get_instance(id)) : nullptr;
}

// Extract a compatible world-space aim from an authored or locally rotated view.
Vector2 view_aim(const Node3D &view) {
    const Vector3 rotation = view.get_global_rotation();
    return Vector2(Math::wrapf(static_cast<double>(rotation.y), -Math::PI, Math::PI),
            Math::clamp(static_cast<double>(rotation.x), -pitch_limit, pitch_limit));
}

// Allow the small rounding error from writing world yaw as float32.
bool valid_yaw(double yaw) {
    return std::isfinite(yaw) && std::abs(yaw) <= Math::PI + 0.00001;
}

// Validate the entire intent before it can mutate per-sender state.
bool valid_intent(const Vector2 &axes, const Vector2 &aim, bool has_lock_heading, double lock_heading) {
    return axes.is_finite() && axes.length_squared() <= 1.00001f && aim.is_finite() &&
            valid_yaw(aim.x) && std::abs(aim.y) <= pitch_limit + 0.00001 &&
            (has_lock_heading ? valid_yaw(lock_heading) : lock_heading == 0.0 && !std::signbit(lock_heading));
}

// Validate the authoritative pose's domain values beyond the wire reader's finite scalar checks.
bool valid_pose(const motion_wire::PoseRow &pose) {
    return pose.peer >= authority_peer && Vector3(pose.x, pose.y, pose.z).is_finite() &&
            valid_intent({}, Vector2(pose.yaw, pose.pitch), false, 0.0) && valid_yaw(pose.body_yaw);
}

// Materialize the pose used by the existing interpolation and admission code.
Vector3 pose_position(const motion_wire::PoseRow &pose) {
    return Vector3(pose.x, pose.y, pose.z);
}

// Reject truncated, extra, empty, or over-budget rows before reading a payload.
bool valid_payload_size(size_t size, size_t row_bytes, size_t max_rows) {
    return size >= row_bytes && size <= row_bytes * max_rows && size % row_bytes == 0;
}

// ENet can reset multiple packets' peers before Godot dispatches their disconnect signals.
// Check the exact targeted connection so movement never sends to an already reset channel.
bool can_send_motion(Node &session, int32_t peer) {
    const Ref<MultiplayerAPI> api = session.get_multiplayer();
    const Ref<MultiplayerPeer> transport = api.is_valid() ? api->get_multiplayer_peer() : Ref<MultiplayerPeer>();
    if (transport.is_null() || transport->get_connection_status() != MultiplayerPeer::CONNECTION_CONNECTED) {
        return false;
    }
    const Ref<ENetMultiplayerPeer> enet = transport;
    if (enet.is_null()) {
        return true;
    }
    const Ref<ENetPacketPeer> connection = enet->get_peer(peer);
    return connection.is_valid() && connection->get_state() == ENetPacketPeer::STATE_CONNECTED;
}

} // namespace

void WorldSession::bind_movement_methods() {
    ClassDB::bind_method(D_METHOD(motion_roster_method, "epoch", "ticket", "revision", "tick", "payload",
                                 "signature", "speed", "reference", "style"),
            &WorldSession::receive_movement_roster);
    ClassDB::bind_method(D_METHOD(motion_input_method, "epoch", "ticket", "newest", "payload"),
            &WorldSession::receive_motion_input);
    ClassDB::bind_method(D_METHOD(motion_pose_method, "epoch", "revision", "tick", "payload"),
            &WorldSession::receive_motion_pose);
    ClassDB::bind_method(D_METHOD(owner_correction_method, "epoch", "revision", "server_tick", "ack",
                                 "position", "velocity", "grounded", "body_yaw"), &WorldSession::receive_owner_correction);
}

void WorldSession::configure_movement_rpc() {
    Dictionary roster;
    roster["rpc_mode"] = MultiplayerAPI::RPC_MODE_AUTHORITY;
    roster["transfer_mode"] = MultiplayerPeer::TRANSFER_MODE_RELIABLE;
    roster["call_local"] = false;
    roster["channel"] = 0;
    rpc_config(motion_roster_method, roster);
    // Godot retains a reference to each Dictionary rather than copying its values.
    Dictionary snapshot = roster.duplicate();
    snapshot["transfer_mode"] = MultiplayerPeer::TRANSFER_MODE_UNRELIABLE;
    rpc_config(motion_pose_method, snapshot);
    rpc_config(owner_correction_method, snapshot);
    Dictionary input = roster.duplicate();
    input["rpc_mode"] = MultiplayerAPI::RPC_MODE_ANY_PEER;
    input["transfer_mode"] = MultiplayerPeer::TRANSFER_MODE_UNRELIABLE_ORDERED;
    rpc_config(motion_input_method, input);
    set_physics_process_priority(mover::Mover::PHYSICS_PRIORITY - 1);
}

void WorldSession::configure_movement(const MovementSettings &settings) {
    if (state == IDLE && !mutating) {
        movement = settings;
    }
}

void WorldSession::clear_movement() {
    set_physics_process(false);
    movement = {};
    motion_revision = 0;
    motion_tick = 0;
    motion_input_sequence = 0;
    motion_input_sent_at = 0;
    motion_snapshot_elapsed = 0.0;
    motion_rate = 0;
    motion_server_tick = 0;
    owner_inputs.clear();
    owner_correction = {};
    owner_ack = 0;
    owner_applied_server_tick = 0;
    owner_correction_count = 0;
    owner_replayed_inputs = 0;
    owner_history_gap = false;
    owner_release_pending = false;
    owner_release_aim = Vector2();
    owner_correction_distance = 0.0;
    owner_view_offset = Vector3();
    owner_view_base = Vector3();
    owner_view_applied = Vector3();
    owner_view_id = ObjectID();
}

void WorldSession::reset_slot_movement(Slot &slot) {
    const Transform3D pose = slot.pose;
    const int32_t peer = slot.peer;
    slot = Slot{ pose, peer };
}

WorldSession *WorldSession::movement_for(Node3D *actor) {
    if (!actor || !actor->is_inside_tree()) {
        return nullptr;
    }
    for (Node *parent = actor->get_parent(); parent; parent = parent->get_parent()) {
        auto *session = Object::cast_to<WorldSession>(parent);
        if (!session || session->movement.rule.is_null() || session->state == IDLE) {
            continue;
        }
        for (const Slot &slot : session->slots) {
            if (slot.actor_id == actor->get_instance_id() && session->get_player(slot.peer) == actor) {
                return session;
            }
        }
    }
    return nullptr;
}

WorldSession::Slot *WorldSession::movement_slot(int64_t peer) {
    if (peer > 0) {
        for (Slot &slot : slots) {
            if (slot.peer == peer) {
                return &slot;
            }
        }
    }
    return nullptr;
}

bool WorldSession::controls_movement_view(Node3D *actor, Node3D *view) {
    if (state != RUNNING || !actor || !view || actor != get_local_player()) {
        return false;
    }
    const auto *slot = movement_slot(transport->get_local_peer_id());
    return slot && slot->camera_id == view->get_instance_id();
}

mover::MoverComponent::ControllerStyle WorldSession::get_controller_style() const {
    return movement.style;
}

bool WorldSession::validate_movement_actor(Node3D *actor) const {
    const auto *body = Object::cast_to<CharacterBody3D>(actor);
    Node3D *camera = nullptr;
    Node3D *hidden = nullptr;
    return body && body->get_up_direction().is_equal_approx(Vector3(0, 1, 0)) &&
            (!movement.rule->is_grounded() || body->get_motion_mode() == CharacterBody3D::MOTION_MODE_GROUNDED) &&
            resolve_view(*actor, camera, hidden) && camera != nullptr;
}

bool WorldSession::attach_movement(Slot &slot, Node3D *actor) {
    auto *body = Object::cast_to<CharacterBody3D>(actor);
    Node3D *camera = nullptr;
    Node3D *hidden = nullptr;
    if (!body || !resolve_view(*actor, camera, hidden) || !camera) {
        return false;
    }
    slot.camera_id = camera->get_instance_id();
    slot.aim = slot.committed_aim = view_aim(*camera);
    const auto pending = pending_guests.find(slot.peer);
    slot.admission_ticket = pending != pending_guests.end() ? pending->second.ticket : 0;

    // Keep the view's authored aim while standing the capsule upright.
    const Vector3 original_rotation = body->get_global_rotation();
    body->set_global_rotation(Vector3(0, original_rotation.y, 0));
    slot.spawn_basis = body->get_global_transform().basis;
    slot.body_yaw = movement.style == mover::MoverComponent::FPS_AIM ? slot.aim.x :
            Math::wrapf(static_cast<double>(original_rotation.y), -Math::PI, Math::PI);
    body->set_global_rotation(Vector3(0, slot.body_yaw, 0));
    camera->set_global_rotation(Vector3(slot.aim.y, slot.aim.x, 0));

    if (active_mode != Mode::Join) {
        slot.mover = mover::Mover::attach(body, movement.rule);
        if (slot.mover.is_null() || slot.mover->use_rule(movement.rule) != OK) {
            return false;
        }
        if (movement.predict && motion_rate <= max_prediction_tick_rate &&
                mover::PredictionDriver::supports(body, movement.rule)) {
            slot.predictor = std::make_unique<mover::PredictionDriver>(body, movement.rule);
            if (!slot.mover->begin_external_steps()) {
                return false;
            }
            slot.simulated.position = body->get_global_position();
        }
        return true;
    }
    // Replicas never collide or run a local movement driver. Render from the network timeline.
    if (const auto prior = mover::Mover::for_node(body); prior.is_valid()) {
        prior->retire_attachment();
    }
    body->set_collision_layer(0);
    // Keep the authored mask for the optional read-only collision probe.
    body->set_physics_interpolation_mode(Node::PHYSICS_INTERPOLATION_MODE_OFF);
    return true;
}

Error WorldSession::submit_player_motion(Node3D *actor, const Vector2 &axes, const Vector2 &aim, bool release,
        bool jump_pressed, bool has_lock_heading, double lock_heading) {
    if (state != RUNNING || movement.rule.is_null() || mutating || !actor || actor != get_local_player()) {
        return ERR_UNAVAILABLE;
    }
    if (!valid_intent(axes, aim, has_lock_heading, lock_heading) ||
            (has_lock_heading && movement.style != mover::MoverComponent::LOCK_ON)) {
        return ERR_INVALID_PARAMETER;
    }
    Slot *slot = movement_slot(transport->get_local_peer_id());
    if (!slot) {
        return ERR_DOES_NOT_EXIST;
    }
    if (active_mode != Mode::Join) {
        slot->local_input_pending = true;
        slot->axes = release ? Vector2() : axes;
        slot->aim = aim;
        slot->jump_pending = !release && (slot->jump_pending || jump_pressed);
        slot->has_lock_heading = !release && has_lock_heading;
        slot->lock_heading = slot->has_lock_heading ? lock_heading : 0.0;
        return OK;
    }
    if (release) {
        owner_release_pending = true;
        owner_release_aim = aim;
        return OK;
    }
    owner_release_pending = false;
    return update_local_motion(*slot, axes, aim, false, jump_pressed, has_lock_heading, lock_heading);
}

Error WorldSession::update_local_motion(Slot &slot, const Vector2 &axes, const Vector2 &aim, bool flush,
        bool jump_pressed, bool has_lock_heading, double lock_heading) {
    if (motion_input_sequence == (std::numeric_limits<int64_t>::max)()) {
        return ERR_OUT_OF_MEMORY;
    }
    reconcile_owner(slot);
    owner_inputs.push_back(MotionInput{ axes, aim, ++motion_input_sequence, jump_pressed, has_lock_heading,
        static_cast<float>(lock_heading) });
    if (owner_inputs.size() > owner_history_limit) {
        owner_inputs.pop_front();
    }
    if (slot.predictor && !owner_history_gap) {
        predict_input(slot, owner_inputs.back());
        set_motion_pose(slot, MotionSample{ slot.simulated.position, aim, slot.body_yaw, 0 }, false);
    }
    const uint64_t now = Time::get_singleton()->get_ticks_msec();
    if (!flush && motion_input_sent_at != 0 && now - motion_input_sent_at < upload_interval_ms) {
        return OK;
    }
    if (!can_send_motion(*this, authority_peer)) {
        return ERR_UNAVAILABLE;
    }
    const size_t count = Math::min(owner_inputs.size(), static_cast<size_t>(command_redundancy));
    const size_t size = count * motion_wire::input_row_bytes;
    PackedByteArray payload;
    if (payload.resize(static_cast<int64_t>(size)) != OK) {
        return ERR_OUT_OF_MEMORY;
    }
    replication::WireWriter writer(payload.ptrw(), size);
    for (size_t index = owner_inputs.size() - count; index < owner_inputs.size(); ++index) {
        const auto &input = owner_inputs[index];
        if (!motion_wire::write_input(writer, { static_cast<float>(input.axes.x), static_cast<float>(input.axes.y),
                    static_cast<float>(input.aim.x), static_cast<float>(input.aim.y), input.jump_pressed,
                    input.has_lock_heading, static_cast<float>(input.lock_heading) })) {
            return ERR_INVALID_DATA;
        }
    }
    motion_input_sent_at = now;
    return rpc_id(authority_peer, motion_input_method, authority_epoch, join_ticket, motion_input_sequence, payload);
}

void WorldSession::predict_input(Slot &slot, MotionInput &input) {
    const double delta = 1.0 / motion_rate;
    const auto step = step_controller(movement.style, movement.reference, slot.spawn_basis, input.axes, input.aim.x,
            slot.body_yaw, movement.speed, delta, input.has_lock_heading, input.lock_heading);
    slot.body_yaw = step.body_yaw;
    slot.simulated = slot.predictor->step(slot.simulated, step.velocity, delta, input.jump_pressed);
    input.predicted = slot.simulated;
    input.predicted_yaw = slot.body_yaw;
    input.has_prediction = true;
}

void WorldSession::receive_motion_input(int64_t epoch, int64_t ticket, int64_t newest,
        const PackedByteArray &payload) {
    const size_t size = static_cast<size_t>(payload.size());
    const int64_t count = static_cast<int64_t>(size / motion_wire::input_row_bytes);
    if (mutating || state != RUNNING || movement.rule.is_null() || epoch != authority_epoch ||
            transport->get_state() != session::GameSession::HOSTING || ticket < 1 || newest < count ||
            !valid_payload_size(size, motion_wire::input_row_bytes, command_redundancy) ||
            newest > (std::numeric_limits<int64_t>::max)() - input_window) {
        return;
    }
    const int32_t sender = get_multiplayer()->get_remote_sender_id();
    Slot *slot = movement_slot(sender);
    if (sender <= authority_peer || !slot || slot->admission_ticket != ticket ||
            newest <= slot->input_sequence || pending_guests.count(sender) != 0) {
        return;
    }
    const uint64_t now = Time::get_singleton()->get_ticks_msec();
    if (slot->input_sequence != 0 && now - slot->received_at < minimum_input_interval_ms) {
        return;
    }
    std::array<motion_wire::InputRow, command_redundancy> commands;
    replication::WireReader reader(payload.ptr(), size);
    for (int64_t row = 0; row < count; ++row) {
        auto &command = commands[row];
        if (!motion_wire::read_input(reader, command) ||
                !valid_intent(Vector2(command.move_x, command.move_y), Vector2(command.yaw, command.pitch),
                        command.has_lock_heading, command.lock_heading) ||
                (command.has_lock_heading && movement.style != mover::MoverComponent::LOCK_ON)) {
            return;
        }
    }
    if (!reader.finished()) {
        return;
    }
    const bool stale = slot->received_at == 0 || now - slot->received_at >= input_lifetime_ms;
    const int64_t oldest_predecessor = newest - count;
    const int64_t newest_allowed = slot->retired_input + input_window;
    if (oldest_predecessor >= newest_allowed && !stale) {
        return;
    }
    if (stale && oldest_predecessor > slot->retired_input) {
        // A resumed sender cannot recover the missing prefix beyond the packet's retained tail.
        // Retire it without applying extra motion or leaving a permanent replay backlog.
        slot->pending_inputs.clear();
        slot->retired_input = oldest_predecessor;
        slot->first_input_at = 0;
        slot->input_started = false;
        slot->axes = Vector2();
        slot->has_lock_heading = false;
        slot->lock_heading = 0.0;
    }
    const int64_t accepted_latest = Math::min(newest, slot->retired_input + input_window);
    if (accepted_latest <= slot->input_sequence) {
        return;
    }
    if (slot->first_input_at == 0) {
        slot->first_input_at = now;
    }
    for (int64_t row = 0; row < count; ++row) {
        const int64_t tick = newest - count + row + 1;
        if (tick > slot->retired_input && tick <= accepted_latest) {
            const auto &command = commands[row];
            slot->pending_inputs.try_emplace(tick, MotionInput{
                Vector2(command.move_x, command.move_y).limit_length(),
                Vector2(command.yaw, command.pitch), tick, command.jump_pressed, command.has_lock_heading,
                command.lock_heading });
        }
    }
    slot->input_sequence = accepted_latest;
    slot->received_at = now;
}

void WorldSession::drive_movement(Slot &slot, const Vector2 &axes, bool jump_pressed) {
    auto *body = Object::cast_to<CharacterBody3D>(find_motion_node(slot.actor_id));
    auto *view = find_motion_node(slot.camera_id);
    if (!body || body->is_queued_for_deletion() || !view || slot.mover.is_null()) {
        return;
    }
    const auto step = step_controller(movement.style, movement.reference, slot.spawn_basis, axes, slot.aim.x,
            slot.body_yaw, movement.speed, 1.0 / motion_rate, slot.has_lock_heading, slot.lock_heading);
    slot.body_yaw = step.body_yaw;
    body->set_global_rotation(Vector3(0, slot.body_yaw, 0));
    view->set_global_rotation(Vector3(slot.aim.y, slot.aim.x, 0));
    slot.committed_aim = slot.aim;
    slot.mover->move_owned(step.velocity, ObjectID(get_instance_id()));
    if (!slot.predictor && jump_pressed) {
        slot.mover->jump();
    }
}

bool WorldSession::capture_motion_pose(const Slot &slot, motion_wire::PoseRow &pose) const {
    const auto *actor = find_motion_node(slot.actor_id);
    if (!actor || actor->is_queued_for_deletion()) {
        return false;
    }
    const Vector3 position = actor->get_global_position();
    pose = { slot.peer, static_cast<float>(position.x), static_cast<float>(position.y), static_cast<float>(position.z),
        static_cast<float>(slot.committed_aim.x), static_cast<float>(slot.committed_aim.y),
        static_cast<float>(slot.body_yaw) };
    return valid_pose(pose);
}

void WorldSession::send_movement_roster(const PackedInt32Array &assignments) {
    const size_t size = static_cast<size_t>(assignments.size() / 2) * motion_wire::roster_row_bytes;
    PackedByteArray payload;
    if (payload.resize(static_cast<int64_t>(size)) != OK) {
        fail_session("spawn_failed");
        return;
    }
    replication::WireWriter writer(payload.ptrw(), size);
    for (int64_t at = 0; at < assignments.size(); at += 2) {
        const int32_t slot_index = assignments[at + 1];
        motion_wire::PoseRow pose;
        if (!capture_motion_pose(slots[slot_index], pose) || !writer.write_i32(slot_index) ||
                !motion_wire::write_pose(writer, pose)) {
            fail_session("spawn_failed");
            return;
        }
    }
    ++motion_revision;
    for (int32_t peer : transport->get_connected_peer_ids()) {
        const auto *slot = movement_slot(peer);
        if (!slot || !can_send_motion(*this, peer)) {
            continue;
        }
        const auto pending = pending_guests.find(peer);
        const int64_t ticket = pending == pending_guests.end() ? 0 : pending->second.ticket;
        const int64_t signature = slot->predictor ? slot->predictor->settings_signature() : 0;
        rpc_id(peer, motion_roster_method, authority_epoch, ticket, motion_revision, motion_tick, payload,
                signature, movement.speed, movement.reference, static_cast<int32_t>(movement.style));
    }
}

void WorldSession::receive_movement_roster(int64_t epoch, int64_t ticket, int64_t revision, int64_t tick,
        const PackedByteArray &payload, int64_t signature, double speed, int32_t reference, int32_t style) {
    if (movement.rule.is_null() || !accepts_authority_rpc() || !get_world() || epoch != authority_epoch ||
            revision <= motion_revision || tick < 0 ||
            (state == STARTING ? join_ticket == 0 || ticket != join_ticket : ticket != 0)) {
        return;
    }
    const size_t size = static_cast<size_t>(payload.size());
    if (!valid_payload_size(size, motion_wire::roster_row_bytes, static_cast<size_t>(player_limit)) ||
            signature < 0 || !std::isfinite(speed) || speed < 0.0 || speed > 1000.0 ||
            reference < PlayerControlProfile::VIEW || reference > PlayerControlProfile::WORLD ||
            style < mover::MoverComponent::FPS_AIM || style > mover::MoverComponent::LOCK_ON) {
        fail_session("invalid_roster");
        return;
    }
    if (style != movement.style) {
        fail_session("incompatible_rules", "The local player and server use different controller styles.");
        return;
    }
    const size_t count = size / motion_wire::roster_row_bytes;
    std::array<motion_wire::PoseRow, MAX_PLAYERS> poses;
    PackedInt32Array assignments;
    if (assignments.resize(static_cast<int64_t>(count * 2)) != OK) {
        fail_session("spawn_failed");
        return;
    }
    int32_t *assignment = assignments.ptrw();
    replication::WireReader reader(payload.ptr(), size);
    for (size_t row = 0; row < count; ++row) {
        auto &pose = poses[row];
        if (!reader.read_i32(assignment[row * 2 + 1]) || !motion_wire::read_pose(reader, pose) ||
                !valid_pose(pose)) {
            fail_session("invalid_roster");
            return;
        }
        assignment[row * 2] = pose.peer;
    }
    PackedInt32Array roster;
    if (!reader.finished() || !expand_assignments(assignments, roster)) {
        fail_session("invalid_roster");
        return;
    }
    if (!commit_authority_roster(roster)) {
        return;
    }
    motion_revision = revision;
    const int32_t local_peer = transport->get_local_peer_id();
    for (size_t row = 0; row < count; ++row) {
        Slot &slot = slots[assignment[row * 2 + 1]];
        const auto &pose = poses[row];
        const MotionSample sample{ pose_position(pose), Vector2(pose.yaw, pose.pitch), pose.body_yaw, tick };
        slot.sample_received_at = Time::get_singleton()->get_ticks_msec();
        if (slot.peer == local_peer && state == STARTING && !baseline_applied) {
            auto *body = Object::cast_to<CharacterBody3D>(find_motion_node(slot.actor_id));
            if (signature != 0 && movement.predict) {
                if (!mover::PredictionDriver::supports(body, movement.rule)) {
                    fail_session("incompatible_rules", "The local player body does not support the server's movement solver.");
                    return;
                }
                auto solver = std::make_unique<mover::PredictionDriver>(body, movement.rule);
                if (solver->settings_signature() != signature) {
                    fail_session("incompatible_rules", "The local player body and server use different movement solver settings.");
                    return;
                }
                slot.predictor = std::move(solver);
                slot.simulated.position = sample.position;
                slot.body_yaw = sample.body_yaw;
                if (auto *view = find_motion_node(slot.camera_id)) {
                    owner_view_id = ObjectID(view->get_instance_id());
                    owner_view_base = view->get_position();
                }
            }
            movement.speed = speed;
            movement.reference = reference;
        }
        const bool initialize = slot.next_sample.tick == 0 && slot.previous_sample.tick == 0;
        if (initialize || state == STARTING) {
            set_motion_pose(slot, sample, true);
            slot.previous_sample = slot.next_sample = sample;
            slot.playback = 1.0;
        } else if (tick > slot.next_sample.tick && !slot.predictor) {
            stage_motion_pose(slot, sample);
        }
    }
    acknowledge_roster();
}

void WorldSession::publish_motion_pose() {
    ++motion_tick;
    const PackedInt32Array recipients = transport->get_connected_peer_ids();
    std::array<motion_wire::PoseRow, snapshot_batch_size> poses;
    size_t count = 0;
    // Size is bounded per datagram. Each slot is a complete, independently useful sample.
    const auto send_batch = [&]() {
        const size_t size = count * motion_wire::pose_row_bytes;
        PackedByteArray payload;
        if (payload.resize(static_cast<int64_t>(size)) != OK) {
            return;
        }
        replication::WireWriter writer(payload.ptrw(), size);
        for (size_t row = 0; row < count; ++row) {
            if (!motion_wire::write_pose(writer, poses[row])) {
                return;
            }
        }
        for (int32_t peer : recipients) {
            if (pending_guests.count(peer) == 0 && movement_slot(peer) && can_send_motion(*this, peer)) {
                rpc_id(peer, motion_pose_method, authority_epoch, motion_revision, motion_tick, payload);
            }
        }
    };
    for (const Slot &slot : slots) {
        if (slot.peer != 0 && capture_motion_pose(slot, poses[count]) && ++count == poses.size()) {
            send_batch();
            count = 0;
        }
    }
    if (count != 0) {
        send_batch();
    }
    for (int32_t peer : recipients) {
        const auto *slot = movement_slot(peer);
        const auto *actor = slot ? find_motion_node(slot->actor_id) : nullptr;
        if (slot && actor && pending_guests.count(peer) == 0 && can_send_motion(*this, peer)) {
            const mover::PredictionState current = slot->predictor ? slot->simulated :
                    mover::PredictionState{ actor->get_global_position(), slot->mover->get_velocity(), false };
            rpc_id(peer, owner_correction_method, authority_epoch, motion_revision, motion_server_tick,
                    slot->committed_input, current.position, current.velocity, current.grounded, slot->body_yaw);
        }
    }
}

void WorldSession::receive_motion_pose(int64_t epoch, int64_t revision, int64_t tick,
        const PackedByteArray &payload) {
    const size_t size = static_cast<size_t>(payload.size());
    if (movement.rule.is_null() || state != RUNNING || !accepts_authority_rpc() ||
            epoch != authority_epoch || revision != motion_revision || tick < 1 ||
            !valid_payload_size(size, motion_wire::pose_row_bytes, snapshot_batch_size)) {
        return;
    }
    const size_t count = size / motion_wire::pose_row_bytes;
    std::array<motion_wire::PoseRow, snapshot_batch_size> poses;
    replication::WireReader reader(payload.ptr(), size);
    for (size_t row = 0; row < count; ++row) {
        if (!motion_wire::read_pose(reader, poses[row]) || !valid_pose(poses[row])) {
            return;
        }
    }
    if (!reader.finished()) {
        return;
    }
    for (size_t row = 0; row < count; ++row) {
        const auto &pose = poses[row];
        Slot *slot = movement_slot(pose.peer);
        if (slot && tick > slot->next_sample.tick && !slot->predictor) {
            slot->sample_received_at = Time::get_singleton()->get_ticks_msec();
            stage_motion_pose(*slot, MotionSample{ pose_position(pose), Vector2(pose.yaw, pose.pitch), pose.body_yaw, tick });
        }
    }
}

void WorldSession::receive_owner_correction(int64_t epoch, int64_t revision, int64_t server_tick, int64_t ack,
        const Vector3 &position, const Vector3 &velocity, bool grounded, double body_yaw) {
    if (movement.rule.is_null() || state != RUNNING || !accepts_authority_rpc() || epoch != authority_epoch ||
            revision != motion_revision || server_tick <= owner_correction.server_tick ||
            ack < owner_ack || ack < owner_correction.acknowledged_input || ack > motion_input_sequence ||
            !position.is_finite() || !velocity.is_finite() || !valid_yaw(body_yaw)) {
        return;
    }
    if (Slot *slot = movement_slot(transport->get_local_peer_id())) {
        slot->sample_received_at = Time::get_singleton()->get_ticks_msec();
        owner_correction = OwnerCorrection{ mover::PredictionState{ position, velocity, grounded }, body_yaw,
            server_tick, ack };
    }
}

void WorldSession::reconcile_owner(Slot &slot) {
    if (owner_correction.server_tick <= owner_applied_server_tick) {
        return;
    }
    owner_applied_server_tick = owner_correction.server_tick;
    owner_ack = owner_correction.acknowledged_input;
    bool agreed = false;
    while (!owner_inputs.empty() && owner_inputs.front().tick <= owner_ack) {
        const auto &input = owner_inputs.front();
        if (input.tick == owner_ack && input.has_prediction) {
            agreed = same_step_state(input.predicted, owner_correction.state, input.predicted_yaw,
                    owner_correction.body_yaw);
        }
        owner_inputs.pop_front();
    }
    owner_replayed_inputs = 0;
    if (!slot.predictor || agreed) {
        return;
    }
    owner_history_gap = (!owner_inputs.empty() && owner_inputs.front().tick != owner_ack + 1) ||
            owner_inputs.size() > maximum_replay_steps;
    const Vector3 before = slot.simulated.position;
    slot.simulated = owner_correction.state;
    slot.body_yaw = owner_correction.body_yaw;
    if (!owner_history_gap) {
        for (MotionInput &input : owner_inputs) {
            predict_input(slot, input);
            ++owner_replayed_inputs;
        }
    } else {
        for (MotionInput &input : owner_inputs) {
            input.has_prediction = false;
        }
        owner_view_offset = Vector3();
    }
    const Vector3 error = before - slot.simulated.position;
    owner_correction_distance = error.length();
    if (owner_correction_distance > correction_threshold) {
        ++owner_correction_count;
        const Vector3 corrected_offset = owner_view_offset + error;
        owner_view_offset = !owner_history_gap && corrected_offset.length() <= correction_snap_distance ?
                corrected_offset : Vector3();
    }
    if (auto *view = find_motion_node(slot.camera_id)) {
        set_motion_pose(slot, MotionSample{ slot.simulated.position, view_aim(*view), slot.body_yaw, 0 }, false);
    }
}

Vector2 WorldSession::take_remote_input(Slot &slot, uint64_t now, bool &jump_pressed) {
    jump_pressed = false;
    if (slot.first_input_at == 0) {
        return Vector2();
    }
    if (!slot.input_started && now - slot.first_input_at >= input_start_cushion_ms) {
        slot.input_started = true;
    }
    if (!slot.input_started) {
        return Vector2();
    }
    if (now - slot.received_at >= input_lifetime_ms) {
        slot.pending_inputs.clear();
        slot.retired_input = slot.input_sequence;
        slot.first_input_at = 0;
        slot.input_started = false;
        slot.axes = Vector2();
        slot.has_lock_heading = false;
        slot.lock_heading = 0.0;
        return Vector2();
    }
    if (slot.retired_input >= slot.input_sequence) {
        // Do not add steering to a state without retiring the corresponding sequence slot.
        return Vector2();
    }
    ++slot.retired_input;
    const auto command = slot.pending_inputs.find(slot.retired_input);
    if (command != slot.pending_inputs.end()) {
        slot.axes = command->second.axes;
        slot.aim = command->second.aim;
        slot.has_lock_heading = command->second.has_lock_heading;
        slot.lock_heading = command->second.lock_heading;
        jump_pressed = command->second.jump_pressed;
        slot.pending_inputs.erase(command);
    }
    return slot.axes;
}

void WorldSession::step_authority(Slot &slot, bool jump_pressed) {
    auto *body = Object::cast_to<CharacterBody3D>(find_motion_node(slot.actor_id));
    if (!slot.predictor || !body || body->is_queued_for_deletion() || slot.mover.is_null()) {
        return;
    }
    const double delta = 1.0 / motion_rate;
    const Vector3 before = slot.simulated.position;
    slot.simulated = slot.predictor->step(slot.simulated, slot.mover->get_external_steering(), delta, jump_pressed);
    body->set_global_position(slot.simulated.position);
    body->set_velocity(slot.simulated.velocity);
    slot.mover->set_external_velocity((slot.simulated.position - before) / delta);
}

WorldSession::MovementDiagnostics WorldSession::get_movement_diagnostics() const {
    MovementDiagnostics result;
    if (state != RUNNING || movement.rule.is_null() || !transport) {
        return result;
    }
    result.mode = active_mode == Mode::Join ? MovementDiagnostics::GUEST_INTERPOLATED :
            MovementDiagnostics::AUTHORITY_LIVE;
    result.simulation_tick = active_mode == Mode::Join ? owner_correction.server_tick : motion_server_tick;
    result.acknowledged_input = owner_ack;
    result.corrections = owner_correction_count;
    result.correction_distance = owner_correction_distance;
    result.replayed_inputs = owner_replayed_inputs;
    const int32_t local = transport->get_local_peer_id();
    const Slot *observed = nullptr;
    for (const Slot &slot : slots) {
        if (slot.peer != 0 && (slot.peer == local || (!observed && active_mode == Mode::Dedicated))) {
            observed = &slot;
        }
    }
    if (!observed) {
        return result;
    }
    if (observed->predictor) {
        result.mode = active_mode == Mode::Join ? MovementDiagnostics::GUEST_PREDICTED :
                MovementDiagnostics::AUTHORITY_STATIC;
    }
    if (auto *actor = find_motion_node(observed->actor_id)) {
        result.predicted_position = actor->get_global_position();
    }
    if (active_mode == Mode::Join) {
        result.pending_inputs = static_cast<int64_t>(owner_inputs.size());
        result.authority_position = observed->predictor && owner_correction.server_tick > 0 ?
                owner_correction.state.position : observed->next_sample.position;
        if (observed->sample_received_at != 0) {
            result.state_age_ms = Time::get_singleton()->get_ticks_msec() - observed->sample_received_at;
        }
    } else {
        result.authority_position = result.predicted_position;
        result.acknowledged_input = observed->committed_input;
        result.pending_inputs = static_cast<int64_t>(observed->pending_inputs.size());
    }
    return result;
}

void WorldSession::stage_motion_pose(Slot &slot, const MotionSample &sample) {
    auto *actor = find_motion_node(slot.actor_id);
    if (!actor) {
        return;
    }
    const Vector2 rendered_aim(
            Math::lerp_angle(static_cast<double>(slot.previous_sample.aim.x),
                    static_cast<double>(slot.next_sample.aim.x), slot.playback),
            Math::lerp(static_cast<double>(slot.previous_sample.aim.y),
                    static_cast<double>(slot.next_sample.aim.y), slot.playback));
    const double rendered_body_yaw = Math::lerp_angle(slot.previous_sample.body_yaw, slot.next_sample.body_yaw,
            slot.playback);
    slot.previous_sample = MotionSample{ actor->get_global_position(), rendered_aim, rendered_body_yaw,
        slot.next_sample.tick };
    slot.playback_seconds = snapshot_interval * Math::clamp(sample.tick - slot.next_sample.tick, int64_t(1), int64_t(2));
    slot.next_sample = sample;
    slot.playback = 0.0;
}

void WorldSession::set_motion_pose(Slot &slot, const MotionSample &sample, bool initialize) {
    auto *actor = find_motion_node(slot.actor_id);
    auto *view = find_motion_node(slot.camera_id);
    if (!actor || !view || actor->is_queued_for_deletion()) {
        return;
    }
    const Vector2 rendered_aim = !initialize && slot.peer == transport->get_local_peer_id() ? view_aim(*view) : sample.aim;
    actor->set_global_position(sample.position);
    actor->set_global_rotation(Vector3(0, sample.body_yaw, 0));
    view->set_global_rotation(Vector3(rendered_aim.y, rendered_aim.x, 0));
    if (initialize) {
        actor->reset_physics_interpolation();
    }
}

void WorldSession::interpolate_movement(double delta) {
    for (Slot &slot : slots) {
        if (slot.peer == 0 || slot.playback >= 1.0) {
            continue;
        }
        slot.playback = Math::min(1.0, slot.playback + delta / slot.playback_seconds);
        const auto &from = slot.previous_sample;
        const auto &to = slot.next_sample;
        const Vector2 aim(Math::lerp_angle(static_cast<double>(from.aim.x), static_cast<double>(to.aim.x), slot.playback),
                Math::lerp(static_cast<double>(from.aim.y), static_cast<double>(to.aim.y), slot.playback));
        const double body_yaw = Math::lerp_angle(from.body_yaw, to.body_yaw, slot.playback);
        set_motion_pose(slot, MotionSample{ from.position.lerp(to.position, slot.playback), aim, body_yaw, to.tick }, false);
    }
    if (owner_view_offset.is_zero_approx() && owner_view_applied.is_zero_approx()) {
        return;
    }
    auto *view = find_motion_node(uint64_t(owner_view_id));
    auto *parent = view ? Object::cast_to<Node3D>(view->get_parent()) : nullptr;
    if (!parent) {
        return;
    }
    const Vector3 current = view->get_position();
    // An animation may author an absolute local position between correction updates.
    if (!current.is_equal_approx(owner_view_base + owner_view_applied)) {
        owner_view_base = current;
    }
    owner_view_offset = owner_view_offset.lerp(Vector3(), 1.0 - std::exp(-delta / 0.065));
    if (owner_view_offset.is_zero_approx()) {
        owner_view_offset = Vector3();
    }
    owner_view_applied = parent->get_global_transform().basis.inverse().xform(owner_view_offset);
    view->set_position(owner_view_base + owner_view_applied);
}

void WorldSession::_physics_process(double delta) {
    if (state != RUNNING || movement.rule.is_null()) {
        return;
    }
    if (active_mode == Mode::Join) {
        if (Slot *slot = movement_slot(transport->get_local_peer_id())) {
            reconcile_owner(*slot);
            if (owner_release_pending) {
                owner_release_pending = false;
                update_local_motion(*slot, Vector2(), owner_release_aim, true, false);
            }
        }
        return;
    }
    motion_snapshot_elapsed += delta;
    if (motion_snapshot_elapsed + 0.000001 >= snapshot_interval) {
        motion_snapshot_elapsed = Math::max(0.0, motion_snapshot_elapsed - snapshot_interval);
        if (!transport->get_connected_peer_ids().is_empty()) {
            publish_motion_pose();
        }
    }
    const uint64_t now = Time::get_singleton()->get_ticks_msec();
    for (Slot &slot : slots) {
        if (slot.mover.is_null()) {
            continue;
        }
        bool jump_pressed = std::exchange(slot.jump_pending, false);
        if (slot.peer > authority_peer) {
            if (pending_guests.count(slot.peer) == 0) {
                const Vector2 axes = take_remote_input(slot, now, jump_pressed);
                drive_movement(slot, axes, jump_pressed);
            }
        } else if (std::exchange(slot.local_input_pending, false)) {
            drive_movement(slot, slot.axes, jump_pressed);
        }
        step_authority(slot, jump_pressed);
        slot.committed_input = slot.retired_input;
    }
    ++motion_server_tick;
}

} // namespace crimson::integration
