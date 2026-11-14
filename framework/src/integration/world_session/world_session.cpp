// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#include "integration/world_session/world_session.hpp"
#include "integration/world_session/local_view.hpp"
#include "integration/world_session/spawn_point.hpp"

#include <godot_cpp/classes/camera3d.hpp>
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/marker3d.hpp>
#include <godot_cpp/classes/scene_tree.hpp>
#include <godot_cpp/classes/time.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/callable_method_pointer.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include <algorithm>
#include <limits>
#include <memory>
#include <set>
#include <utility>

namespace crimson::integration {

using namespace godot;

namespace {

// Stable names for the native transport and the locally mounted world.
constexpr const char *transport_name = "Transport";
constexpr const char *world_name = "World";
constexpr const char *players_name = "Players";
// Reliable channel zero orders each roster after native session admission.
constexpr const char *roster_method = "_crimson_world_session_roster_v1";
// Immediate play sends only occupied peer and candidate pairs and supports a scoped denial.
constexpr const char *offer_method = "_crimson_world_session_offer_v3";
constexpr const char *movement_offer_method = "_crimson_world_session_offer_v5";
constexpr const char *mounted_method = "_crimson_world_session_mounted_v3";
constexpr const char *assignments_method = "_crimson_world_session_assignments_v3";
constexpr const char *baseline_applied_method = "_crimson_world_session_baseline_applied_v3";
constexpr const char *join_ready_method = "_crimson_world_session_join_ready_v3";
constexpr const char *spawn_rejection_method = "_crimson_world_session_spawn_rejected_v3";
// The first slot belongs to the listen host or the offline player.
constexpr int32_t authority_peer = 1;
// Bound map preflight independently from the number of seats available to guests.
constexpr int32_t max_spawn_candidates = 256;
// Bound the time a native-admitted guest can wait for its first world roster.
constexpr double spawn_timeout_seconds = 8.0;
// Allow the shared loader's default preparation time and a separate baseline acknowledgment.
constexpr uint64_t guest_stage_timeout_ms = 18000;

// Hold the mutation guard across synchronous notifications from game-owned nodes.
class Mutation {
    // The owning session's guarded flag and its previous value.
    bool &flag;
    bool previous;

public:
    // Block further public startup and leave actions for the duration of the mutation.
    explicit Mutation(bool &value) : flag(value), previous(value) { flag = true; }
    // Restore the previous guard, including when an error returns early.
    ~Mutation() { flag = previous; }
};

// Resolve weak references to authored scene nodes without retaining dangling pointers.
Node3D *find_spatial(uint64_t id) {
    return id ? Object::cast_to<Node3D>(ObjectDB::get_instance(id)) : nullptr;
}

// Restrict scene configuration to paths relative to the owning instance.
bool valid_local_path(const NodePath &path) {
    return !path.is_absolute() && path.get_subname_count() == 0;
}

// Delete an instance that never entered the world.
struct DeleteDetached {
    // The detached node still belongs to the staging code.
    void operator()(Node *node) const { memdelete(node); }
};

using DetachedNode = std::unique_ptr<Node, DeleteDetached>;

// Return the marker pose relative to the world, including nested spatial parents.
bool marker_pose(const Node3D &world, const Marker3D &marker, Transform3D &pose) {
    if (marker.is_set_as_top_level()) {
        return false;
    }
    pose = marker.get_transform();
    for (const Node *parent = marker.get_parent(); parent != &world; parent = parent->get_parent()) {
        const auto *spatial = Object::cast_to<Node3D>(parent);
        if (!spatial || spatial->is_set_as_top_level()) {
            return false;
        }
        pose = spatial->get_transform() * pose;
    }
    return pose.is_finite();
}

// Discover typed world or actor components in deterministic scene order.
template <typename T>
void collect_components(Node &root, std::vector<T *> &matches, size_t limit) {
    for (int index = 0; index < root.get_child_count() && matches.size() < limit; ++index) {
        Node *child = root.get_child(index);
        if (auto *component = Object::cast_to<T>(child)) {
            matches.push_back(component);
        }
        if (matches.size() < limit) {
            collect_components(*child, matches, limit);
        }
    }
}

// A viewport can auto-activate its first camera even when the actor declares it inactive.
void clear_dedicated_cameras(Node &node) {
    if (auto *camera = Object::cast_to<Camera3D>(&node); camera && camera->is_current()) {
        camera->clear_current(false);
    }
    for (int index = 0; index < node.get_child_count(); ++index) {
        clear_dedicated_cameras(*node.get_child(index));
    }
}

} // namespace

void WorldSession::_bind_methods() {
    ClassDB::bind_static_method(get_class_static(), D_METHOD("configure"), &WorldSession::configure);
    ClassDB::bind_method(D_METHOD("set_definition", "definition"), &WorldSession::set_definition);
    ClassDB::bind_method(D_METHOD("get_definition"), &WorldSession::get_definition);
    ClassDB::bind_method(D_METHOD("get_port"), &WorldSession::get_port);
    ClassDB::bind_method(D_METHOD("start_offline"), &WorldSession::start_offline);
    ClassDB::bind_method(D_METHOD("host"), &WorldSession::host);
    ClassDB::bind_method(D_METHOD("join", "address"), &WorldSession::join);
    ClassDB::bind_method(D_METHOD("leave"), &WorldSession::leave);
    ClassDB::bind_method(D_METHOD("get_state"), &WorldSession::get_state);
    ClassDB::bind_method(D_METHOD("get_world"), &WorldSession::get_world);
    ClassDB::bind_method(D_METHOD("get_local_player"), &WorldSession::get_local_player);
    ClassDB::bind_method(D_METHOD("get_player", "peer_id"), &WorldSession::get_player);
    ClassDB::bind_method(D_METHOD(roster_method, "roster"), &WorldSession::receive_roster);
    ClassDB::bind_method(D_METHOD(offer_method, "world_id", "epoch", "ticket", "candidate_count", "host_limit", "dedicated"),
            &WorldSession::receive_world_offer);
    ClassDB::bind_method(D_METHOD(movement_offer_method, "world_id", "epoch", "ticket", "candidate_count", "host_limit", "dedicated", "tick_rate"),
            &WorldSession::receive_movement_offer);
    bind_movement_methods();
    ClassDB::bind_method(D_METHOD(mounted_method, "epoch", "ticket"), &WorldSession::receive_scene_mounted);
    ClassDB::bind_method(D_METHOD(assignments_method, "epoch", "ticket", "assignments"), &WorldSession::receive_assignments);
    ClassDB::bind_method(D_METHOD(baseline_applied_method, "epoch", "ticket"), &WorldSession::receive_baseline_applied);
    ClassDB::bind_method(D_METHOD(join_ready_method, "epoch", "ticket"), &WorldSession::receive_join_ready);
    ClassDB::bind_method(D_METHOD(spawn_rejection_method, "reason"), &WorldSession::receive_spawn_rejection);
    ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "definition", PROPERTY_HINT_RESOURCE_TYPE,
                         "WorldSessionDefinition"),
            "set_definition", "get_definition");
    ADD_SIGNAL(MethodInfo("state_changed", PropertyInfo(Variant::INT, "state")));
    ADD_SIGNAL(MethodInfo("session_ended", PropertyInfo(Variant::STRING_NAME, "reason")));
    BIND_ENUM_CONSTANT(IDLE);
    BIND_ENUM_CONSTANT(STARTING);
    BIND_ENUM_CONSTANT(RUNNING);
}

void WorldSession::_enter_tree() {
    set_multiplayer_authority(authority_peer, false);
    Dictionary config;
    config["rpc_mode"] = MultiplayerAPI::RPC_MODE_AUTHORITY;
    config["transfer_mode"] = MultiplayerPeer::TRANSFER_MODE_RELIABLE;
    config["call_local"] = false;
    config["channel"] = 0;
    rpc_config(roster_method, config);
    rpc_config(offer_method, config);
    rpc_config(movement_offer_method, config);
    rpc_config(assignments_method, config);
    rpc_config(join_ready_method, config);
    rpc_config(spawn_rejection_method, config);
    Dictionary reports = config.duplicate();
    reports["rpc_mode"] = MultiplayerAPI::RPC_MODE_ANY_PEER;
    rpc_config(mounted_method, reports);
    rpc_config(baseline_applied_method, reports);
    configure_movement_rpc();
}

void WorldSession::_ready() {
    if (!transport) {
        transport = memnew(session::GameSession);
        transport->set_name(transport_name);
        add_child(transport, false, INTERNAL_MODE_BACK);
    }
    transport->connect("session_started", callable_mp(this, &WorldSession::on_transport_started));
    transport->connect("session_ended", callable_mp(this, &WorldSession::on_transport_ended));
    transport->connect("peer_joined", callable_mp(this, &WorldSession::on_peer_joined));
    transport->connect("peer_left", callable_mp(this, &WorldSession::on_peer_left));
    if (scene_loader) {
        scene_loader->connect("load_completed", callable_mp(this, &WorldSession::on_world_loaded));
        scene_loader->connect("load_failed", callable_mp(this, &WorldSession::on_world_load_failed));
        scene_loader->connect("load_sync_failed", callable_mp(this, &WorldSession::on_world_load_failed));
    }
    set_process(false);
    set_physics_process(false);
}

void WorldSession::_exit_tree() {
    const Mutation guard(mutating);
    clear_world(true);
    state = IDLE;
}

Ref<WorldSessionDefinition> WorldSession::configure() {
    return WorldSessionDefinition::configure();
}

void WorldSession::set_definition(const Ref<WorldSessionDefinition> &value) {
    if (state == IDLE && !mutating) {
        definition = value;
    }
}

Ref<WorldSessionDefinition> WorldSession::get_definition() const {
    return definition;
}

void WorldSession::configure_immediate(const Ref<PackedScene> &world, const Ref<PackedScene> &player,
        const StringName &key, int64_t max_total_players, int64_t listen_port, int64_t epoch) {
    if (state != IDLE || mutating) {
        return;
    }
    Ref<WorldSessionDefinition> config = WorldSessionDefinition::configure();
    config->set_world_scene(world);
    config->set_player_scene(player);
    config->set_port(listen_port);
    definition = config;
    world_key = key;
    player_limit = max_total_players;
    authority_epoch = epoch;
}

void WorldSession::use_services(session::GameSession *session, scene_load::SceneLoadManager *loader) {
    if (!is_inside_tree()) {
        transport = session;
        scene_loader = loader;
    }
}

Error WorldSession::host_immediate(const String &bind_address, bool dedicated, bool hold_admission) {
    return start(dedicated ? Mode::Dedicated : Mode::Host, bind_address, {}, hold_admission);
}

Error WorldSession::join_endpoint(const Ref<session::SessionEndpoint> &endpoint) {
    return start(Mode::Join, {}, endpoint);
}

int64_t WorldSession::get_world_epoch() const {
    return authority_epoch;
}

String WorldSession::get_last_end_detail() const {
    return last_end_detail;
}

bool WorldSession::is_authority() const {
    return transport && transport->is_authority();
}

int64_t WorldSession::get_port() const {
    if (state != IDLE) {
        return port;
    }
    return definition.is_valid() ? definition->get_port() : WorldSessionDefinition::DEFAULT_PORT;
}

Error WorldSession::start_offline() {
    return start(Mode::Offline);
}

Error WorldSession::host() {
    return start(Mode::Host, "*");
}

Error WorldSession::join(const String &address) {
    return start(Mode::Join, address.strip_edges());
}

Error WorldSession::leave() {
    if (mutating || (player_limit != 0 && scene_loader && scene_loader->is_publishing())) {
        return ERR_BUSY;
    }
    if (state != IDLE) {
        transport->leave();
    }
    return OK;
}

WorldSession::State WorldSession::get_state() const {
    return state;
}

Node3D *WorldSession::get_world() const {
    if (player_limit != 0 && scene_loader) {
        Node *world = scene_loader->get_current_scene();
        return world && world->get_parent() == this ? Object::cast_to<Node3D>(world) : nullptr;
    }
    Node3D *world = find_spatial(world_id);
    return world && world->get_parent() == this && !world->is_queued_for_deletion() ? world : nullptr;
}

Node3D *WorldSession::get_local_player() const {
    return transport && active_mode != Mode::Dedicated ? get_player(transport->get_local_peer_id()) : nullptr;
}

int64_t WorldSession::get_local_player_number() const {
    const Node3D *actor = get_local_player();
    if (!actor) {
        return 0;
    }
    const uint64_t actor_id = actor->get_instance_id();
    for (size_t index = 0; index < slots.size(); ++index) {
        if (slots[index].actor_id == actor_id) {
            return static_cast<int64_t>(index) + 1;
        }
    }
    return 0;
}

Node3D *WorldSession::get_player(int64_t peer_id) const {
    Node3D *world = get_world();
    Node3D *players = find_spatial(players_id);
    if (peer_id <= 0 || !world || !players || players->get_parent() != world || players->is_queued_for_deletion()) {
        return nullptr;
    }
    for (const Slot &slot : slots) {
        if (slot.peer == peer_id) {
            Node3D *actor = find_spatial(slot.actor_id);
            return actor && actor->get_parent() == players &&
                            !actor->is_queued_for_deletion() ? actor : nullptr;
        }
    }
    return nullptr;
}

bool WorldSession::is_peer_world_ready(int32_t peer_id) const {
    return state == RUNNING && is_authority() && peer_id > 0 &&
            pending_guests.count(peer_id) == 0 && get_player(peer_id) != nullptr;
}

int32_t WorldSession::available_remote_seats() const {
    if (state != RUNNING || !transport || !is_authority()) {
        return 0;
    }
    const int32_t total = static_cast<int32_t>(slots.size());
    const int32_t guests = static_cast<int32_t>(transport->get_connected_peer_ids().size());
    const int32_t local = get_local_player() ? 1 : 0;
    return total > guests + local ? total - guests - local : 0;
}

Error WorldSession::start(Mode mode, const String &address,
        const Ref<session::SessionEndpoint> &endpoint, bool hold_admission) {
    if (state != IDLE || mutating) {
        return ERR_BUSY;
    }
    if (!is_inside_tree() || !transport || definition.is_null()) {
        return ERR_UNCONFIGURED;
    }
    if (mode == Mode::Join && endpoint.is_null() && address.is_empty()) {
        return ERR_INVALID_PARAMETER;
    }
    {
        const Mutation guard(mutating);
        Error error = configure_start(mode);
        if (error == OK) {
            admission_held = hold_admission;
            switch (mode) {
                case Mode::Offline:
                    error = transport->start_offline();
                    break;
                case Mode::Host:
                    error = player_limit == 0 ? transport->host(port, static_cast<int64_t>(slots.size()) - 1, address) :
                                                transport->host_with_admission(port, player_limit - 1, address, false);
                    break;
                case Mode::Join:
                    error = endpoint.is_valid() ? transport->join_endpoint(endpoint) : transport->join(address, port);
                    break;
                case Mode::Dedicated:
                    error = transport->host_with_admission(port, player_limit, address, false);
                    break;
            }
        }
        if (error != OK) {
            clear_world();
            return error;
        }
        if (player_limit != 0) {
            if (mode != Mode::Join && (error = request_world()) != OK) {
                transport->leave();
                clear_world();
                return error;
            }
            state = STARTING;
        } else if (mode == Mode::Join) {
            state = STARTING;
        } else {
            PackedInt32Array roster = get_roster();
            roster.set(0, authority_peer);
            if (!apply_roster(roster)) {
                transport->leave();
                clear_world();
                return ERR_INVALID_DATA;
            }
            state = RUNNING;
        }
    }
    emit_signal("state_changed", state);
    return OK;
}

Error WorldSession::configure_start(Mode mode) {
    active_mode = mode;
    if (movement.rule.is_valid() && mode != Mode::Join) {
        motion_rate = Engine::get_singleton()->get_physics_ticks_per_second();
    }
    const Ref<PackedScene> scene = definition->get_world_scene();
    player_scene = definition->get_player_scene();
    camera_path = definition->get_local_camera_path();
    hidden_path = definition->get_local_hidden_path();
    port = definition->get_port();
    const NodePath markers_path = definition->get_spawn_points_path();
    if (scene.is_null() || !scene->can_instantiate() || player_scene.is_null() ||
            !player_scene->can_instantiate() || (player_limit == 0 && markers_path.is_empty()) ||
            get_node_or_null(NodePath(world_name))) {
        return ERR_UNCONFIGURED;
    }
    if (!valid_local_path(markers_path) || !valid_local_path(camera_path) || !valid_local_path(hidden_path) ||
            port < 1 || port > 65535) {
        return ERR_INVALID_PARAMETER;
    }
    if (player_limit != 0) {
        if (!scene_loader || player_limit < 1 || player_limit > MAX_PLAYERS ||
                (mode == Mode::Host && player_limit == 1)) {
            return ERR_INVALID_PARAMETER;
        }
        const Error mount = scene_loader->set_world_container(this);
        if (mount == OK) {
            scene_loader->set_coordinated_enabled(false);
        }
        return mount;
    }
    DetachedNode staged(scene->instantiate());
    const Error error = static_cast<Error>(configure_candidate(staged.get()));
    if (error == OK) {
        staged.release();
    }
    return error;
}

int64_t WorldSession::configure_candidate(Node *candidate) {
    const Mutation guard(mutating);
    auto *world = Object::cast_to<Node3D>(candidate);
    if (!world || world->get_node_or_null(NodePath(players_name))) {
        return ERR_INVALID_DATA;
    }
    std::vector<const Marker3D *> candidates;
    if (player_limit != 0) {
        std::vector<PlayerSpawnPoint *> anchors;
        collect_components(*world, anchors, max_spawn_candidates + 1);
        for (const auto *anchor : anchors) {
            candidates.push_back(anchor);
            for (int index = 0; index < anchor->get_child_count() && candidates.size() <= max_spawn_candidates; ++index) {
                Node *child = anchor->get_child(index);
                if (!Object::cast_to<PlayerSpawnPoint>(child)) {
                    if (auto *marker = Object::cast_to<Marker3D>(child)) {
                        candidates.push_back(marker);
                    }
                }
            }
            if (candidates.size() > max_spawn_candidates) {
                return ERR_INVALID_DATA;
            }
        }
        if (anchors.empty()) {
            const auto *markers = world->get_node_or_null(NodePath("SpawnPoints"));
            if (!markers || markers->get_child_count() > max_spawn_candidates) {
                return ERR_INVALID_DATA;
            }
            for (int index = 0; index < markers->get_child_count(); ++index) {
                candidates.push_back(Object::cast_to<Marker3D>(markers->get_child(index)));
            }
        }
    } else {
        const NodePath markers_path = definition->get_spawn_points_path();
        auto *markers = world->get_node_or_null(markers_path);
        if (!markers || (markers != world && !world->is_ancestor_of(markers))) {
            return ERR_INVALID_DATA;
        }
        const int32_t minimum = active_mode == Mode::Offline ? 1 : 2;
        if (markers->get_child_count() < minimum || markers->get_child_count() > MAX_PLAYERS) {
            return ERR_INVALID_DATA;
        }
        for (int index = 0; index < markers->get_child_count(); ++index) {
            candidates.push_back(Object::cast_to<Marker3D>(markers->get_child(index)));
        }
    }
    if (candidates.empty()) {
        return ERR_INVALID_DATA;
    }
    slots.reserve(candidates.size());
    for (const auto *marker : candidates) {
        Transform3D pose;
        if (!marker || !marker_pose(*world, *marker, pose) ||
                std::any_of(slots.begin(), slots.end(), [&](const Slot &slot) { return slot.pose.origin == pose.origin; })) {
            return ERR_INVALID_DATA;
        }
        slots.push_back(Slot{ pose });
    }
    Node3D *first_actor = instantiate_actor();
    if (!first_actor) {
        return ERR_INVALID_DATA;
    }
    if (movement.rule.is_valid() && !validate_movement_actor(first_actor)) {
        memdelete(first_actor);
        return ERR_INVALID_DATA;
    }
    first_actor_id = first_actor->get_instance_id();
    world->set_name(world_name);
    auto *players = memnew(Node3D);
    players->set_name(players_name);
    world->add_child(players);
    world_id = world->get_instance_id();
    players_id = players->get_instance_id();
    return OK;
}

Error WorldSession::request_world() {
    return scene_loader->request_local_load(world_key, definition->get_world_scene(),
            callable_mp(this, &WorldSession::configure_candidate));
}

void WorldSession::on_world_loaded(const StringName &scene_id) {
    if (player_limit == 0 || state != STARTING || scene_id != world_key) {
        return;
    }
    if (!get_world()) {
        fail_session("scene_load_failed", "The starting world was no longer mounted after scene activation.");
        return;
    }
    if (active_mode == Mode::Join) {
        if (expected_candidates != static_cast<int64_t>(slots.size())) {
            fail_session("incompatible_world");
        } else {
            const Error sent = rpc_id(authority_peer, mounted_method, authority_epoch, join_ticket);
            if (sent != OK) {
                fail_session("connection_failed", "Could not confirm the local world mount to the host (" +
                        UtilityFunctions::error_string(sent) + String(")."));
            }
        }
        return;
    }
    bool spawned = true;
    {
        const Mutation guard(mutating);
        if (active_mode != Mode::Dedicated) {
            PackedInt32Array roster = get_roster();
            roster.set(0, authority_peer);
            spawned = apply_roster(roster);
        } else {
            clear_dedicated_cameras(*get_world());
        }
    }
    if (!spawned) {
        fail_session("spawn_failed");
        return;
    }
    if (!admission_held && (active_mode == Mode::Host || active_mode == Mode::Dedicated)) {
        const Error admission = transport->set_accepting_connections(true);
        if (admission != OK) {
            fail_session("connection_failed", "Could not open admission after mounting the starting world (" +
                    UtilityFunctions::error_string(admission) + String(")."));
            return;
        }
    }
    finish_start();
}

void WorldSession::on_world_load_failed(const StringName &scene_id, int64_t error, const String &message) {
    if (!mutating && player_limit != 0 && state == STARTING && scene_id == world_key) {
        fail_session(error == ERR_INVALID_DATA ? "invalid_spawn_setup" : "scene_load_failed",
                message.is_empty() ? "The scene loader reported " + UtilityFunctions::error_string(error) + String(".") :
                                     message);
    }
}

Node3D *WorldSession::instantiate_actor() const {
    DetachedNode staged(player_scene->instantiate());
    auto *actor = Object::cast_to<Node3D>(staged.get());
    if (!actor || actor->is_set_as_top_level()) {
        return nullptr;
    }
    Node3D *camera = nullptr;
    Node3D *hidden = nullptr;
    if (!resolve_view(*actor, camera, hidden)) {
        return nullptr;
    }
    if (camera) {
        Object::cast_to<Camera3D>(camera)->set_current(false);
    }
    actor->set_multiplayer_authority(authority_peer);
    staged.release();
    return actor;
}

bool WorldSession::resolve_view(Node3D &actor, Node3D *&camera, Node3D *&hidden) const {
    camera = nullptr;
    hidden = nullptr;
    if (player_limit != 0) {
        std::vector<LocalPlayerView *> views;
        collect_components(actor, views, 2);
        if (views.size() > 1) {
            return false;
        }
        if (!views.empty()) {
            camera = views.front()->get_camera();
            hidden = views.front()->get_hidden_visuals();
        } else {
            std::vector<Camera3D *> cameras;
            collect_components(actor, cameras, 2);
            if (cameras.size() == 1) {
                camera = cameras.front();
            }
            for (int index = 0; index < actor.get_child_count(); ++index) {
                auto *child = Object::cast_to<Node3D>(actor.get_child(index));
                if (child && child->is_in_group("crimson_local_hidden")) {
                    if (hidden) {
                        return false;
                    }
                    hidden = child;
                }
            }
        }
    } else {
        camera = camera_path.is_empty() ? nullptr : Object::cast_to<Camera3D>(actor.get_node_or_null(camera_path));
        hidden = hidden_path.is_empty() ? nullptr : Object::cast_to<Node3D>(actor.get_node_or_null(hidden_path));
        if ((!camera_path.is_empty() && !camera) || (!hidden_path.is_empty() && !hidden)) {
            return false;
        }
    }
    return (!camera || (actor.is_ancestor_of(camera) && !camera->is_queued_for_deletion())) &&
            (!hidden || (actor.is_ancestor_of(hidden) && !hidden->is_queued_for_deletion()));
}

bool WorldSession::apply_roster(const PackedInt32Array &next) {
    const uint64_t expected_world = world_id;
    const uint64_t expected_players = players_id;
    Node3D *world = nullptr;
    Node3D *players = nullptr;
    // Scene notifications can retire an owned node while adding or removing a child.
    const auto resolve_scene = [&]() {
        world = find_spatial(expected_world);
        players = find_spatial(expected_players);
        return world_id == expected_world && players_id == expected_players &&
                world && !world->is_queued_for_deletion() && (!world->get_parent() || world->get_parent() == this) &&
                players && !players->is_queued_for_deletion() && players->get_parent() == world;
    };
    if (!resolve_scene()) {
        return false;
    }
    std::vector<DetachedNode> additions(slots.size());
    const int32_t local_peer = active_mode == Mode::Dedicated ? 0 : transport->get_local_peer_id();
    for (int64_t index = 0; index < next.size(); ++index) {
        const Slot &slot = slots[index];
        if (next[index] == 0 || next[index] == slot.peer) {
            continue;
        }
        Node3D *actor = find_spatial(first_actor_id);
        first_actor_id = 0;
        if (!actor) {
            actor = instantiate_actor();
        }
        if (!actor) {
            return false;
        }
        additions[index].reset(actor);
        if (movement.rule.is_valid() && !validate_movement_actor(actor)) {
            return false;
        }
        actor->set_name(String("Peer_") + String::num_int64(next[index]));
        actor->set_transform(slot.pose);
        if (next[index] == local_peer) {
            Node3D *camera = nullptr;
            Node3D *hidden = nullptr;
            if (!resolve_view(*actor, camera, hidden)) {
                return false;
            }
            if (hidden) {
                hidden->hide();
            }
        }
    }

    bool new_local_player = false;
    for (int64_t index = 0; index < next.size(); ++index) {
        Slot &slot = slots[index];
        if (next[index] == slot.peer) {
            continue;
        }
        const uint64_t previous_id = slot.actor_id;
        if (Node3D *previous = find_spatial(previous_id); previous && previous->get_parent() == players) {
            players->remove_child(previous);
            if (Node3D *retired = find_spatial(previous_id); retired && !retired->get_parent()) {
                retired->queue_free();
            }
            if (!resolve_scene()) {
                return false;
            }
        }
        slot.peer = next[index];
        slot.actor_id = 0;
        reset_slot_movement(slot);
        if (additions[index]) {
            slot.actor_id = additions[index]->get_instance_id();
            const uint64_t added_id = slot.actor_id;
            new_local_player = new_local_player || slot.peer == local_peer;
            players->add_child(additions[index].release());
            if (!resolve_scene()) {
                return false;
            }
            Node3D *actor = find_spatial(added_id);
            if (!actor || actor->is_queued_for_deletion() || actor->get_parent() != players) {
                return false;
            }
            if (movement.rule.is_valid() && !attach_movement(slot, actor)) {
                return false;
            }
            if (active_mode == Mode::Dedicated) {
                clear_dedicated_cameras(*actor);
            }
        }
    }
    if (player_limit == 0 && !world->get_parent()) {
        add_child(world);
    }
    if (active_mode == Mode::Dedicated) {
        return get_world() != nullptr;
    }
    Node3D *local = get_local_player();
    if (!local) {
        return false;
    }
    if (new_local_player) {
        Node3D *camera = nullptr;
        Node3D *hidden = nullptr;
        if (!resolve_view(*local, camera, hidden)) {
            return false;
        }
        if (camera) {
            Object::cast_to<Camera3D>(camera)->make_current();
        }
    }
    return true;
}

PackedInt32Array WorldSession::get_roster() const {
    PackedInt32Array roster;
    roster.resize(slots.size());
    for (size_t index = 0; index < slots.size(); ++index) {
        roster.set(index, slots[index].peer);
    }
    return roster;
}

bool WorldSession::valid_roster(const PackedInt32Array &roster) const {
    if (roster.size() != static_cast<int64_t>(slots.size()) || roster.is_empty() ||
            (remote_dedicated ? roster.has(authority_peer) : roster[0] != authority_peer)) {
        return false;
    }
    std::set<int32_t> seen;
    for (int64_t index = 0; index < roster.size(); ++index) {
        const int32_t peer = roster[index];
        if (peer < 0 || (peer != 0 && !seen.insert(peer).second)) {
            return false;
        }
        const int32_t previous = slots[index].peer;
        if (previous != 0 && previous != peer && roster.has(previous)) {
            return false;
        }
    }
    return seen.count(transport->get_local_peer_id()) != 0;
}

void WorldSession::publish_roster(const PackedInt32Array &roster, int32_t joining_peer) {
    bool applied;
    bool guest_rejected = false;
    {
        const Mutation guard(mutating);
        const PackedInt32Array previous = get_roster();
        applied = apply_roster(roster);
        if (!applied && player_limit != 0 && joining_peer > authority_peer && get_world() &&
                (active_mode == Mode::Dedicated || get_local_player())) {
            guest_rejected = get_roster() == previous || apply_roster(previous);
        }
    }
    if (!applied) {
        if (guest_rejected) {
            reject_spawn(joining_peer);
        } else {
            fail_session("spawn_failed");
        }
        return;
    }
    if (player_limit == 0) {
        for (int32_t recipient : transport->get_connected_peer_ids()) {
            rpc_id(recipient, roster_method, roster);
        }
        return;
    }
    PackedInt32Array assignments;
    for (int64_t index = 0; index < roster.size(); ++index) {
        if (roster[index] > 0) {
            assignments.append(roster[index]);
            assignments.append(static_cast<int32_t>(index));
        }
    }
    if (movement.rule.is_valid()) {
        send_movement_roster(assignments);
        return;
    }
    for (int32_t recipient : transport->get_connected_peer_ids()) {
        if (roster.has(recipient)) {
            const auto pending = pending_guests.find(recipient);
            const int64_t ticket = pending == pending_guests.end() ? 0 : pending->second.ticket;
            rpc_id(recipient, assignments_method, authority_epoch, ticket, assignments);
        }
    }
}

void WorldSession::receive_roster(const PackedInt32Array &roster) {
    if (player_limit == 0 && accepts_authority_rpc()) {
        apply_authority_roster(roster);
    }
}

void WorldSession::receive_world_offer(const StringName &received_world, int64_t epoch, int64_t ticket,
        int64_t candidate_count, int64_t host_limit, bool dedicated) {
    accept_world_offer(received_world, epoch, ticket, candidate_count, host_limit, dedicated, false);
}

void WorldSession::receive_movement_offer(const StringName &received_world, int64_t epoch, int64_t ticket,
        int64_t candidate_count, int64_t host_limit, bool dedicated, int32_t tick_rate) {
    if (state != STARTING || join_ticket != 0 || !accepts_authority_rpc()) {
        return;
    }
    if (tick_rate < 1 || tick_rate > 1000 || tick_rate != Engine::get_singleton()->get_physics_ticks_per_second()) {
        fail_session("incompatible_rules", "The server and client must use the same physics tick rate for player movement.");
        return;
    }
    motion_rate = tick_rate;
    accept_world_offer(received_world, epoch, ticket, candidate_count, host_limit, dedicated, true);
}

void WorldSession::accept_world_offer(const StringName &received_world, int64_t epoch, int64_t ticket,
        int64_t candidate_count, int64_t host_limit, bool dedicated, bool controlled) {
    if (player_limit == 0 || state != STARTING || join_ticket != 0 || !accepts_authority_rpc()) {
        return;
    }
    if (received_world != world_key || candidate_count < 1 || candidate_count > max_spawn_candidates ||
            epoch < 1 || ticket < 1) {
        fail_session("incompatible_world");
        return;
    }
    if (host_limit != player_limit || controlled != movement.rule.is_valid()) {
        fail_session("incompatible_rules");
        return;
    }
    authority_epoch = epoch;
    join_ticket = ticket;
    expected_candidates = candidate_count;
    remote_dedicated = dedicated;
    start_spawn_deadline(2.0 * guest_stage_timeout_ms / 1000.0);
    const Error requested = request_world();
    if (requested != OK) {
        fail_session("scene_load_failed", "The scene loader rejected the starting world (" +
                UtilityFunctions::error_string(requested) + String(")."));
    }
}

void WorldSession::receive_assignments(int64_t epoch, int64_t ticket, const PackedInt32Array &assignments) {
    if (movement.rule.is_valid() || player_limit == 0 || !accepts_authority_rpc() || !get_world() || epoch != authority_epoch ||
            (state == STARTING ? join_ticket == 0 || ticket != join_ticket : ticket != 0)) {
        return;
    }
    PackedInt32Array roster;
    if (!expand_assignments(assignments, roster)) {
        fail_session("invalid_roster");
        return;
    }
    apply_authority_roster(roster);
}

bool WorldSession::expand_assignments(const PackedInt32Array &assignments, PackedInt32Array &roster) const {
    if (assignments.size() < 2 || assignments.size() % 2 != 0 || assignments.size() > player_limit * 2) {
        return false;
    }
    roster.resize(slots.size());
    for (int64_t index = 0; index < assignments.size(); index += 2) {
        const int32_t peer = assignments[index];
        const int32_t point = assignments[index + 1];
        if (peer <= 0 || point < 0 || point >= roster.size() || roster[point] != 0) {
            return false;
        }
        roster.set(point, peer);
    }
    return valid_roster(roster);
}

int32_t WorldSession::reporting_guest(int64_t epoch, int64_t ticket, GuestPhase phase) const {
    if (mutating || player_limit == 0 || state != RUNNING || epoch != authority_epoch ||
            transport->get_state() != session::GameSession::HOSTING) {
        return 0;
    }
    const int32_t sender = get_multiplayer()->get_remote_sender_id();
    const auto pending = pending_guests.find(sender);
    return pending != pending_guests.end() && pending->second.ticket == ticket && pending->second.phase == phase &&
                    Time::get_singleton()->get_ticks_msec() < pending->second.deadline &&
                    transport->get_connected_peer_ids().has(sender) ? sender : 0;
}

void WorldSession::receive_scene_mounted(int64_t epoch, int64_t ticket) {
    const int32_t sender = reporting_guest(epoch, ticket, GuestPhase::Mounting);
    if (!sender) {
        return;
    }
    PackedInt32Array roster = get_roster();
    const int64_t vacancy = roster.find(0, active_mode == Mode::Dedicated ? 0 : 1);
    if (vacancy < 0) {
        reject_spawn(sender);
        return;
    }
    auto &pending = pending_guests.at(sender);
    pending.phase = GuestPhase::ApplyingBaseline;
    pending.deadline = Time::get_singleton()->get_ticks_msec() + guest_stage_timeout_ms;
    roster.set(vacancy, sender);
    publish_roster(roster, sender);
}

void WorldSession::receive_baseline_applied(int64_t epoch, int64_t ticket) {
    const int32_t sender = reporting_guest(epoch, ticket, GuestPhase::ApplyingBaseline);
    if (!sender || !get_player(sender)) {
        return;
    }
    if (rpc_id(sender, join_ready_method, authority_epoch, ticket) != OK) {
        on_peer_left(sender);
        reject_spawn(sender, "connection_failed");
        return;
    }
    pending_guests.erase(sender);
    set_process(!pending_guests.empty());
}

void WorldSession::receive_join_ready(int64_t epoch, int64_t ticket) {
    if (player_limit != 0 && state == STARTING && accepts_authority_rpc() && baseline_applied &&
            epoch == authority_epoch && ticket == join_ticket && get_local_player()) {
        finish_start();
    }
}

bool WorldSession::accepts_authority_rpc() const {
    return !mutating && state != IDLE && transport->get_state() == session::GameSession::CLIENT &&
            get_multiplayer()->get_remote_sender_id() == authority_peer;
}

void WorldSession::receive_spawn_rejection(const StringName &reason) {
    if (player_limit != 0 && state == STARTING && accepts_authority_rpc()) {
        if (reason == StringName("spawn_unavailable") || reason == StringName("join_timeout") ||
                reason == StringName("connection_failed")) {
            fail_session(reason);
        }
    }
}

void WorldSession::reject_spawn(int32_t peer, const StringName &reason) {
    pending_guests.erase(peer);
    set_process(!pending_guests.empty());
    rpc_id(peer, spawn_rejection_method, reason);
    transport->disconnect_admitted_peer_after_flush(peer);
}

void WorldSession::apply_authority_roster(const PackedInt32Array &roster) {
    if (!valid_roster(roster)) {
        fail_session("invalid_roster");
        return;
    }
    if (commit_authority_roster(roster)) {
        acknowledge_roster();
    }
}

bool WorldSession::commit_authority_roster(const PackedInt32Array &roster) {
    bool applied;
    {
        const Mutation guard(mutating);
        applied = apply_roster(roster);
    }
    if (!applied) {
        fail_session("spawn_failed");
    }
    return applied;
}

void WorldSession::acknowledge_roster() {
    if (state == STARTING) {
        if (player_limit == 0) {
            finish_start();
        } else if (!baseline_applied) {
            baseline_applied = true;
            if (rpc_id(authority_peer, baseline_applied_method, authority_epoch, join_ticket) != OK) {
                fail_session("connection_failed");
            }
        }
    }
}

void WorldSession::finish_start() {
    clear_spawn_deadline();
    state = RUNNING;
    set_physics_process(movement.rule.is_valid());
    if (movement.rule.is_valid() && active_mode == Mode::Join) {
        set_process(true);
    }
    emit_signal("state_changed", state);
}

void WorldSession::on_transport_started(int64_t role, int64_t) {
    if (role == session::GameSession::CLIENT) {
        start_spawn_deadline(spawn_timeout_seconds);
    }
}

void WorldSession::on_transport_ended(const StringName &reason) {
    if (state == IDLE) {
        return;
    }
    const bool local_failure = !failure_reason.is_empty();
    const StringName resolved = local_failure ? failure_reason : reason;
    const String detail = failure_detail;
    {
        const Mutation guard(mutating);
        clear_world();
        state = IDLE;
        emit_signal("state_changed", state);
    }
    last_end_detail = detail;
    if (!scene_loader && local_failure) {
        const String explanation = !detail.is_empty() ? detail : resolved == StringName("spawn_timeout") ?
                String("The host did not provide the local player's spawn assignment in time.") :
                String("The world session could not apply the required player assignments.");
        UtilityFunctions::printerr("[Crimson] World session ended (", resolved, "). ", explanation);
    }
    emit_signal("session_ended", resolved);
}

void WorldSession::on_peer_joined(int64_t peer_id) {
    if (mutating || state != RUNNING || transport->get_state() != session::GameSession::HOSTING) {
        return;
    }
    if (player_limit != 0) {
        const int32_t peer = static_cast<int32_t>(peer_id);
        if (next_ticket == (std::numeric_limits<int64_t>::max)()) {
            reject_spawn(peer, "connection_failed");
            return;
        }
        const int64_t ticket = ++next_ticket;
        pending_guests.emplace(peer, PendingGuest{ ticket, Time::get_singleton()->get_ticks_msec() + guest_stage_timeout_ms });
        const Error offered = movement.rule.is_valid() ?
                rpc_id(peer, movement_offer_method, world_key, authority_epoch, ticket, int64_t(slots.size()),
                        player_limit, active_mode == Mode::Dedicated, motion_rate) :
                rpc_id(peer, offer_method, world_key, authority_epoch, ticket, int64_t(slots.size()),
                        player_limit, active_mode == Mode::Dedicated);
        if (offered != OK) {
            reject_spawn(peer, "connection_failed");
        } else {
            set_process(true);
        }
        return;
    }
    PackedInt32Array roster = get_roster();
    if (roster.has(peer_id)) {
        return;
    }
    const int64_t vacancy = roster.find(0, 1);
    if (vacancy != -1) {
        roster.set(vacancy, static_cast<int32_t>(peer_id));
        publish_roster(roster, static_cast<int32_t>(peer_id));
    }
}

void WorldSession::on_peer_left(int64_t peer_id) {
    if (mutating || state != RUNNING || transport->get_state() != session::GameSession::HOSTING) {
        return;
    }
    pending_guests.erase(static_cast<int32_t>(peer_id));
    set_process(!pending_guests.empty());
    PackedInt32Array roster = get_roster();
    const int64_t index = roster.find(peer_id, active_mode == Mode::Dedicated ? 0 : 1);
    if (index != -1) {
        roster.set(index, 0);
        publish_roster(roster);
    }
}

void WorldSession::_process(double delta) {
    if (state == RUNNING && movement.rule.is_valid() && active_mode == Mode::Join) {
        interpolate_movement(delta);
        return;
    }
    if (state != RUNNING || transport->get_state() != session::GameSession::HOSTING) {
        return;
    }
    const uint64_t now = Time::get_singleton()->get_ticks_msec();
    std::vector<int32_t> expired;
    for (const auto &[peer, pending] : pending_guests) {
        if (now >= pending.deadline) {
            expired.push_back(peer);
        }
    }
    for (int32_t peer : expired) {
        if (state != RUNNING || pending_guests.count(peer) == 0) {
            break;
        }
        on_peer_left(peer);
        if (state == RUNNING) {
            reject_spawn(peer, "join_timeout");
        }
    }
}

void WorldSession::on_spawn_timeout() {
    if (state == STARTING) {
        fail_session("spawn_timeout");
    }
}

void WorldSession::clear_spawn_deadline() {
    if (spawn_deadline.is_valid()) {
        spawn_deadline->disconnect("timeout", callable_mp(this, &WorldSession::on_spawn_timeout));
        spawn_deadline.unref();
    }
}

void WorldSession::start_spawn_deadline(double seconds) {
    clear_spawn_deadline();
    spawn_deadline = get_tree()->create_timer(seconds, true, false, true);
    spawn_deadline->connect("timeout", callable_mp(this, &WorldSession::on_spawn_timeout));
}

void WorldSession::fail_session(const StringName &reason, const String &detail) {
    if (state != IDLE) {
        failure_reason = reason;
        failure_detail = detail;
        transport->leave();
    }
}

void WorldSession::clear_world(bool exiting) {
    clear_movement();
    clear_spawn_deadline();
    if (Node3D *staged_actor = find_spatial(first_actor_id)) {
        memdelete(staged_actor);
    }
    first_actor_id = 0;
    const uint64_t retired_id = std::exchange(world_id, 0);
    if (player_limit != 0 && scene_loader && (exiting || scene_loader->get_world_container() == this)) {
        if (!exiting) {
            if (scene_loader->is_loading()) {
                scene_loader->cancel_load();
            }
            scene_loader->clear_world();
        }
    } else if (Node3D *world = find_spatial(retired_id)) {
        if (world->get_parent() == this && !exiting) {
            remove_child(world);
        }
        if (Node3D *retired = find_spatial(retired_id); retired &&
                (!retired->get_parent() || (exiting && retired->get_parent() == this))) {
            retired->queue_free();
        }
    }
    slots.clear();
    players_id = 0;
    player_scene.unref();
    camera_path = NodePath();
    hidden_path = NodePath();
    port = 0;
    authority_epoch = 0;
    join_ticket = 0;
    expected_candidates = 0;
    remote_dedicated = false;
    baseline_applied = false;
    admission_held = false;
    pending_guests.clear();
    set_process(false);
    failure_reason = StringName();
    failure_detail = String();
}

} // namespace crimson::integration
