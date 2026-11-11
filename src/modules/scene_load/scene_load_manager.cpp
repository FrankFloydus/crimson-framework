// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#include "scene_load_manager.hpp"

#include <godot_cpp/classes/offline_multiplayer_peer.hpp>
#include <godot_cpp/classes/packed_scene.hpp>
#include <godot_cpp/classes/resource_loader.hpp>
#include <godot_cpp/classes/scene_tree.hpp>
#include <godot_cpp/classes/scene_tree_timer.hpp>
#include <godot_cpp/classes/time.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/callable_method_pointer.hpp>

#include <algorithm>
#include <limits>
#include <utility>

namespace crimson::scene_load {

using namespace godot;

namespace {

constexpr int64_t server_id = MultiplayerPeer::TARGET_PEER_SERVER;
constexpr int64_t maximum_timeout_msec = 120000;
constexpr uint64_t client_timeout_grace_msec = 1000;
constexpr size_t maximum_abandoned_loads = 32;
constexpr const char *world_name = "World";
constexpr const char *prepare_method = "_crimson_scene_prepare";
constexpr const char *ready_method = "_crimson_scene_ready";
constexpr const char *commit_method = "_crimson_scene_commit";
constexpr const char *applied_method = "_crimson_scene_applied";
constexpr const char *finalize_method = "_crimson_scene_finalize";
constexpr const char *abort_method = "_crimson_scene_abort";

// Read the monotonic clock for transition deadlines.
uint64_t ticks_msec() {
    return Time::get_singleton()->get_ticks_msec();
}

// Resolve an instance ID without keeping the node alive.
Node *find_node(uint64_t id) {
    return id ? Object::cast_to<Node>(ObjectDB::get_instance(id)) : nullptr;
}

// Recognize Godot's local transport mode.
bool is_offline(const Ref<MultiplayerPeer> &transport) {
    return transport.is_null() || Object::cast_to<OfflineMultiplayerPeer>(transport.ptr()) != nullptr;
}

// Check that a peer can participate in an active request.
bool is_transport_connected(const Ref<MultiplayerPeer> &transport) {
    return transport.is_valid() && transport->get_connection_status() == MultiplayerPeer::CONNECTION_CONNECTED;
}

// Snapshot the scope's remote peers for a transition barrier.
std::set<int64_t> get_members(const Ref<MultiplayerAPI> &network) {
    const PackedInt32Array peers = network->get_peers();
    std::set<int64_t> members;
    for (int64_t index = 0; index < peers.size(); ++index) {
        members.insert(peers[index]);
    }
    return members;
}

// Validate an identifier before resolving it against local registration.
bool valid_scene_id(const StringName &scene_id) {
    const String text = scene_id;
    if (text.is_empty() || text.length() > 64) {
        return false;
    }
    for (int64_t index = 0; index < text.length(); ++index) {
        const char32_t ch = text[index];
        const bool alnum = (ch >= U'a' && ch <= U'z') ||
                (ch >= U'A' && ch <= U'Z') || (ch >= U'0' && ch <= U'9');
        if (!alnum && (index == 0 || (ch != U'_' && ch != U'-' && ch != U'.'))) {
            return false;
        }
    }
    return true;
}

// Check the bounds for preparation and confirmation deadlines.
bool valid_timeout(int64_t timeout) {
    return timeout > 0 && timeout <= maximum_timeout_msec;
}

// Convert a peer's error code to a supported Godot error.
Error read_remote_error(int64_t error) {
    return error > OK && error <= ERR_PRINTER_ON_FIRE ? static_cast<Error>(error) : ERR_CONNECTION_ERROR;
}

// Collect a threaded request once it can be read without blocking.
bool collect_finished_resource(const String &path) {
    ResourceLoader *loader = ResourceLoader::get_singleton();
    const auto status = loader->load_threaded_get_status(path);
    if (status == ResourceLoader::THREAD_LOAD_IN_PROGRESS) {
        return false;
    }
    if (status != ResourceLoader::THREAD_LOAD_INVALID_RESOURCE) {
        loader->load_threaded_get(path);
    }
    return true;
}

// A manager can leave the tree before Godot's uncancellable loader completes.
void drain_abandoned_resource(uint64_t tree_id, const String &path) {
    if (collect_finished_resource(path)) {
        return;
    }
    SceneTree *tree = Object::cast_to<SceneTree>(ObjectDB::get_instance(tree_id));
    if (tree) {
        Ref<SceneTreeTimer> timer = tree->create_timer(0.1, true, false, true);
        timer->connect("timeout", callable_mp_static(&drain_abandoned_resource).bind(tree_id, path));
    }
}

} // namespace

CrimsonSceneLoadManager::CrimsonSceneLoadManager() {
    set_process_mode(PROCESS_MODE_ALWAYS);
}

void CrimsonSceneLoadManager::_bind_methods() {
    ClassDB::bind_method(D_METHOD("set_world_container", "container"), &CrimsonSceneLoadManager::set_world_container);
    ClassDB::bind_method(D_METHOD("get_world_container"), &CrimsonSceneLoadManager::get_world_container);
    ClassDB::bind_method(D_METHOD("register_scene", "scene_id", "scene_path"), &CrimsonSceneLoadManager::register_scene);
    ClassDB::bind_method(D_METHOD("request_load", "scene_id"), &CrimsonSceneLoadManager::request_load);
    ClassDB::bind_method(D_METHOD("request_coordinated_load", "scene_id", "publish_state"),
            &CrimsonSceneLoadManager::request_coordinated_load);
    ClassDB::bind_method(D_METHOD("cancel_load"), &CrimsonSceneLoadManager::cancel_load);
    ClassDB::bind_method(D_METHOD("clear_world"), &CrimsonSceneLoadManager::clear_world);
    ClassDB::bind_method(D_METHOD("is_loading"), &CrimsonSceneLoadManager::is_loading);
    ClassDB::bind_method(D_METHOD("get_current_scene"), &CrimsonSceneLoadManager::get_current_scene);
    ClassDB::bind_method(D_METHOD("get_current_scene_id"), &CrimsonSceneLoadManager::get_current_scene_id);
    ClassDB::bind_method(D_METHOD("set_load_timeout_msec", "timeout"), &CrimsonSceneLoadManager::set_load_timeout_msec);
    ClassDB::bind_method(D_METHOD("get_load_timeout_msec"), &CrimsonSceneLoadManager::get_load_timeout_msec);
    ClassDB::bind_method(D_METHOD("set_manage_admission", "enabled"), &CrimsonSceneLoadManager::set_manage_admission);
    ClassDB::bind_method(D_METHOD("get_manage_admission"), &CrimsonSceneLoadManager::get_manage_admission);

    ClassDB::bind_method(D_METHOD(prepare_method, "token", "scene_id", "timeout"), &CrimsonSceneLoadManager::rpc_prepare);
    ClassDB::bind_method(D_METHOD(ready_method, "token", "error"), &CrimsonSceneLoadManager::rpc_ready);
    ClassDB::bind_method(D_METHOD(commit_method, "token"), &CrimsonSceneLoadManager::rpc_commit);
    ClassDB::bind_method(D_METHOD(applied_method, "token", "error"), &CrimsonSceneLoadManager::rpc_applied);
    ClassDB::bind_method(D_METHOD(finalize_method, "token"), &CrimsonSceneLoadManager::rpc_finalize);
    ClassDB::bind_method(D_METHOD(abort_method, "token", "error", "after_commit"), &CrimsonSceneLoadManager::rpc_abort);

    ADD_SIGNAL(MethodInfo("load_started", PropertyInfo(Variant::STRING_NAME, "scene_id")));
    ADD_SIGNAL(MethodInfo("load_completed", PropertyInfo(Variant::STRING_NAME, "scene_id")));
    ADD_SIGNAL(MethodInfo("load_failed", PropertyInfo(Variant::STRING_NAME, "scene_id"),
            PropertyInfo(Variant::INT, "error"), PropertyInfo(Variant::STRING, "message")));
    ADD_SIGNAL(MethodInfo("load_sync_failed", PropertyInfo(Variant::STRING_NAME, "scene_id"),
            PropertyInfo(Variant::INT, "error"), PropertyInfo(Variant::STRING, "message")));
}

void CrimsonSceneLoadManager::_enter_tree() {
    leaving_tree = false;
    set_multiplayer_authority(server_id, false);
    Dictionary config;
    config["rpc_mode"] = MultiplayerAPI::RPC_MODE_AUTHORITY;
    config["transfer_mode"] = MultiplayerPeer::TRANSFER_MODE_RELIABLE;
    config["call_local"] = false;
    config["channel"] = 0;
    rpc_config(prepare_method, config);
    rpc_config(commit_method, config);
    rpc_config(finalize_method, config);
    rpc_config(abort_method, config);

    // rpc_config retains the dictionary. Use a new one for client messages.
    Dictionary report = config.duplicate();
    report["rpc_mode"] = MultiplayerAPI::RPC_MODE_ANY_PEER;
    rpc_config(ready_method, report);
    rpc_config(applied_method, report);
    set_process(true);
}

void CrimsonSceneLoadManager::_exit_tree() {
    leaving_tree = true;
    if (pending) {
        Transition old = std::move(*pending);
        pending.reset();
        discard_candidate(old);
    }
    SceneTree *tree = get_tree();
    if (tree) {
        for (const String &path : abandoned_loads) {
            drain_abandoned_resource(tree->get_instance_id(), path);
        }
    }
    abandoned_loads.clear();
    release_admission_gate();
    last_server_transport_id = 0;
    last_server_token = 0;
}

Error CrimsonSceneLoadManager::set_world_container(Node *container) {
    if (pending || publishing_world) {
        return ERR_BUSY;
    }
    if (leaving_tree || !is_inside_tree() || !container || !container->is_inside_tree() ||
            container->is_queued_for_deletion() || container->get_tree() != get_tree()) {
        return ERR_INVALID_PARAMETER;
    }
    if (get_current_scene() && container_id != container->get_instance_id()) {
        return ERR_ALREADY_IN_USE;
    }
    container_id = container->get_instance_id();
    return OK;
}

Node *CrimsonSceneLoadManager::get_world_container() const {
    Node *container = find_node(container_id);
    return is_inside_tree() && container && container->is_inside_tree() &&
            !container->is_queued_for_deletion() && container->get_tree() == get_tree() ? container : nullptr;
}

Error CrimsonSceneLoadManager::register_scene(const StringName &scene_id, const String &scene_path) {
    if (pending || publishing_world) {
        return ERR_BUSY;
    }
    if (!valid_scene_id(scene_id) || scene_path.length() > 512 ||
            !scene_path.begins_with("res://") || scene_path.contains("\\") ||
            scene_path.simplify_path() != scene_path ||
            !(scene_path.ends_with(".tscn") || scene_path.ends_with(".scn"))) {
        return ERR_INVALID_PARAMETER;
    }
    if (scenes.has(scene_id)) {
        return ERR_ALREADY_EXISTS;
    }
    scenes[scene_id] = scene_path;
    return OK;
}

bool CrimsonSceneLoadManager::slot_is_available() {
    Node *container = get_world_container();
    if (!container) {
        return false;
    }
    Node *current = get_current_scene();
    Node *occupant = container->get_node_or_null(NodePath(world_name));
    return (!current || (current != this && !current->is_ancestor_of(this))) &&
            (!occupant || occupant == current);
}

Node *CrimsonSceneLoadManager::get_staged_scene() const {
    Node *candidate = pending ? find_node(pending->candidate_id) : nullptr;
    return candidate && !candidate->get_parent() && !candidate->is_queued_for_deletion() ? candidate : nullptr;
}

Error CrimsonSceneLoadManager::request_load(const StringName &scene_id) {
    return begin_load(scene_id, Callable());
}

Error CrimsonSceneLoadManager::request_coordinated_load(const StringName &scene_id, const Callable &publish_state) {
    if (!publish_state.is_valid()) {
        return ERR_INVALID_PARAMETER;
    }
    return begin_load(scene_id, publish_state);
}

Error CrimsonSceneLoadManager::begin_load(const StringName &scene_id, const Callable &publish_state) {
    if (pending || publishing_world) {
        return ERR_BUSY;
    }
    if (leaving_tree || !is_inside_tree()) {
        return ERR_UNCONFIGURED;
    }

    Transition transition;
    transition.network = get_multiplayer();
    if (transition.network.is_valid()) {
        transition.transport = transition.network->get_multiplayer_peer();
    }
    if (!is_offline(transition.transport)) {
        if (!is_transport_connected(transition.transport)) {
            return ERR_CONNECTION_ERROR;
        }
        if (!transition.network->is_server() || transition.network->get_unique_id() != server_id) {
            return ERR_UNAUTHORIZED;
        }
        if (!manage_admission && !transition.transport->is_refusing_new_connections()) {
            return ERR_UNCONFIGURED;
        }
        transition.role = Role::Host;
    }
    if (!slot_is_available()) {
        return ERR_UNCONFIGURED;
    }
    if (!scenes.has(scene_id)) {
        return ERR_DOES_NOT_EXIST;
    }
    if (next_token == (std::numeric_limits<int64_t>::max)()) {
        return ERR_OUT_OF_MEMORY;
    }

    transition.token = ++next_token;
    transition.scene_id = scene_id;
    transition.resource_path = scenes[scene_id];
    transition.publish_state = publish_state;
    transition.timeout_msec = load_timeout_msec;
    transition.deadline_msec = ticks_msec() + transition.timeout_msec;
    if (transition.role == Role::Host) {
        if (manage_admission) {
            acquire_admission_gate(transition.transport);
        }
        transition.members = get_members(transition.network);
    }
    const int64_t token = transition.token;
    pending = std::move(transition);
    emit_signal("load_started", scene_id);
    if (pending && pending->token == token && pending->role == Role::Host && !pending->members.empty()) {
        if (!transport_matches(*pending) || rpc(prepare_method, token, scene_id, pending->timeout_msec) != OK) {
            finish_failure(ERR_CONNECTION_ERROR, "The scene preparation could not reach every peer.");
        }
    }
    return OK;
}

Error CrimsonSceneLoadManager::cancel_load() {
    if (!pending) {
        return ERR_DOES_NOT_EXIST;
    }
    if (pending->role == Role::Client) {
        return ERR_UNAUTHORIZED;
    }
    if (publishing_world || pending->phase == Phase::CommitOrdered || pending->phase == Phase::Applied) {
        return ERR_BUSY;
    }
    finish_failure(ERR_SKIP, "The scene transition was canceled.");
    return OK;
}

Error CrimsonSceneLoadManager::clear_world() {
    if (pending || publishing_world) {
        return ERR_BUSY;
    }
    if (Node *world = get_current_scene()) {
        const uint64_t removed_id = world->get_instance_id();
        publishing_world = true;
        world->get_parent()->remove_child(world);
        publishing_world = false;
        if (Node *removed = find_node(removed_id)) {
            if (removed == get_current_scene()) {
                return ERR_BUSY;
            }
            if (!removed->get_parent()) {
                removed->queue_free();
            }
        }
    }
    world_id = 0;
    current_scene_id = StringName();
    release_admission_gate();
    return OK;
}

bool CrimsonSceneLoadManager::is_loading() const {
    return pending.has_value();
}

Node *CrimsonSceneLoadManager::get_current_scene() const {
    Node *world = find_node(world_id);
    Node *container = get_world_container();
    return world && container && !world->is_queued_for_deletion() && world->get_parent() == container ? world : nullptr;
}

StringName CrimsonSceneLoadManager::get_current_scene_id() const {
    return get_current_scene() ? current_scene_id : StringName();
}

Error CrimsonSceneLoadManager::set_load_timeout_msec(int64_t timeout) {
    if (pending) {
        return ERR_BUSY;
    }
    if (!valid_timeout(timeout)) {
        return ERR_INVALID_PARAMETER;
    }
    load_timeout_msec = timeout;
    return OK;
}

int64_t CrimsonSceneLoadManager::get_load_timeout_msec() const {
    return load_timeout_msec;
}

Error CrimsonSceneLoadManager::set_manage_admission(bool enabled) {
    if (pending || publishing_world || gated_transport.is_valid()) {
        return ERR_BUSY;
    }
    manage_admission = enabled;
    return OK;
}

bool CrimsonSceneLoadManager::get_manage_admission() const {
    return manage_admission;
}

bool CrimsonSceneLoadManager::transport_matches(const Transition &transition) const {
    const Ref<MultiplayerAPI> current = get_multiplayer();
    const Ref<MultiplayerPeer> peer = current.is_valid() ? current->get_multiplayer_peer() : Ref<MultiplayerPeer>();
    if (current != transition.network || peer != transition.transport) {
        return false;
    }
    if (transition.role == Role::Offline) {
        return is_offline(peer);
    }
    return is_transport_connected(peer) &&
            (transition.role == Role::Host ? current->is_server() && current->get_unique_id() == server_id &&
                    peer->is_refusing_new_connections() && get_members(current) == transition.members
                                            : !current->is_server() && get_members(current).count(server_id) != 0);
}

void CrimsonSceneLoadManager::acquire_admission_gate(const Ref<MultiplayerPeer> &transport) {
    if (gated_transport != transport) {
        release_admission_gate();
        gated_transport = transport;
        restore_refuse_connections = transport->is_refusing_new_connections();
    }
    transport->set_refuse_new_connections(true);
}

void CrimsonSceneLoadManager::release_admission_gate() {
    if (gated_transport.is_valid()) {
        gated_transport->set_refuse_new_connections(restore_refuse_connections);
        gated_transport.unref();
    }
    gate_has_committed_world = false;
    gate_requires_recovery = false;
}

void CrimsonSceneLoadManager::reconcile_admission_gate() {
    if (!last_server_transport_id && gated_transport.is_null()) {
        return;
    }
    const Ref<MultiplayerAPI> network = get_multiplayer();
    const Ref<MultiplayerPeer> peer = network.is_valid() ? network->get_multiplayer_peer() : Ref<MultiplayerPeer>();
    if (last_server_transport_id &&
            (!is_transport_connected(peer) || peer->get_instance_id() != last_server_transport_id)) {
        last_server_transport_id = 0;
        last_server_token = 0;
    }
    if (gated_transport.is_valid()) {
        if (peer != gated_transport || !is_transport_connected(peer) ||
                (!gate_requires_recovery && !get_current_scene())) {
            release_admission_gate();
        } else if (!peer->is_refusing_new_connections()) {
            peer->set_refuse_new_connections(true);
        }
    }
}

void CrimsonSceneLoadManager::poll_abandoned_loads() {
    abandoned_loads.erase(std::remove_if(abandoned_loads.begin(), abandoned_loads.end(), collect_finished_resource),
            abandoned_loads.end());
}

void CrimsonSceneLoadManager::_process(double) {
    if (publishing_world) {
        return;
    }
    poll_abandoned_loads();
    if (!pending) {
        reconcile_admission_gate();
        return;
    }
    if (!transport_matches(*pending)) {
        finish_failure(ERR_CONNECTION_ERROR, "The scene transition's network membership changed.");
        return;
    }
    if (ticks_msec() >= pending->deadline_msec) {
        finish_failure(ERR_TIMEOUT, "The scene transition timed out.");
        return;
    }

    switch (pending->phase) {
        case Phase::Preparing:
            prepare_world();
            break;
        case Phase::Ready:
            if (pending->role != Role::Client && pending->ready_peers.size() == pending->members.size()) {
                order_commit();
            }
            break;
        case Phase::CommitOrdered:
            apply_commit();
            break;
        case Phase::Applied:
            if (!get_current_scene()) {
                finish_failure(ERR_CANT_CREATE, "The committed world was removed before synchronization completed.");
            } else if (pending->role == Role::Host && pending->applied_peers.size() == pending->members.size()) {
                finish_host_barrier();
            }
            break;
    }
}

void CrimsonSceneLoadManager::prepare_world() {
    ResourceLoader *loader = ResourceLoader::get_singleton();
    if (!pending->resource_requested) {
        if (pending->resource_path.is_empty() || !loader->exists(pending->resource_path, "PackedScene")) {
            finish_failure(ERR_DOES_NOT_EXIST, "The requested scene is unavailable on this peer.");
            return;
        }
        const auto existing = std::find(abandoned_loads.begin(), abandoned_loads.end(), pending->resource_path);
        if (existing != abandoned_loads.end()) {
            abandoned_loads.erase(existing);
        } else {
            if (abandoned_loads.size() >= maximum_abandoned_loads) {
                finish_failure(ERR_BUSY, "Too many previous scene resources are still loading.");
                return;
            }
            const Error error = loader->load_threaded_request(pending->resource_path, "PackedScene");
            if (error != OK) {
                finish_failure(error, "The scene resource could not be scheduled for loading.");
                return;
            }
        }
        pending->resource_requested = true;
        return;
    }

    const auto status = loader->load_threaded_get_status(pending->resource_path);
    if (status == ResourceLoader::THREAD_LOAD_IN_PROGRESS) {
        return;
    }
    pending->resource_requested = false;
    Ref<PackedScene> packed;
    if (status != ResourceLoader::THREAD_LOAD_INVALID_RESOURCE) {
        packed = loader->load_threaded_get(pending->resource_path);
    }
    if (status != ResourceLoader::THREAD_LOAD_LOADED || packed.is_null() || !packed->can_instantiate()) {
        finish_failure(ERR_CANT_OPEN, "The scene resource could not be loaded as a PackedScene.");
        return;
    }
    const int64_t token = pending->token;
    Node *world = packed->instantiate();
    if (!pending || pending->token != token) {
        if (world) {
            world->queue_free();
        }
        return;
    }
    if (!world || world->is_queued_for_deletion()) {
        finish_failure(ERR_CANT_CREATE, "The scene could not be staged.");
        return;
    }
    pending->candidate_id = world->get_instance_id();
    if (!slot_is_available()) {
        finish_failure(ERR_UNCONFIGURED, "The persistent world container is unavailable.");
        return;
    }
    pending->phase = Phase::Ready;
    if (pending->role == Role::Client && rpc_id(server_id, ready_method, token, static_cast<int64_t>(OK)) != OK) {
        finish_failure(ERR_CONNECTION_ERROR, "Scene readiness could not reach the host.");
    }
}

void CrimsonSceneLoadManager::order_commit() {
    if (!slot_is_available() || !get_staged_scene()) {
        finish_failure(ERR_UNCONFIGURED, "The staged scene or world container is unavailable.");
        return;
    }
    const Callable publish_state = pending->publish_state;
    if (!publish_state.is_null()) {
        if (!publish_state.is_valid()) {
            finish_failure(ERR_UNAVAILABLE, "The authority state publisher is no longer available.");
            return;
        }
        const int64_t token = pending->token;
        publishing_world = true;
        const Variant result = publish_state.call();
        publishing_world = false;
        if (!pending || pending->token != token) {
            return;
        }
        const int64_t code = result.get_type() == Variant::INT ? int64_t(result) : -1;
        if (code != OK) {
            const Error error = code > OK && code <= ERR_PRINTER_ON_FIRE ? static_cast<Error>(code) : ERR_INVALID_DATA;
            finish_failure(error, "The authority state could not be published.");
            return;
        }
        pending->state_published = true;
        // State observers must leave the mount and transport intact until scene publication completes.
        if (!slot_is_available() || !get_staged_scene()) {
            finish_failure(ERR_UNCONFIGURED, "The staged scene or world container became unavailable after state publication.");
            return;
        }
        if (!transport_matches(*pending)) {
            finish_failure(ERR_CONNECTION_ERROR, "The scene transition's network membership changed after state publication.");
            return;
        }
    }
    pending->phase = Phase::CommitOrdered;
    pending->deadline_msec = ticks_msec() + pending->timeout_msec;
    // Once the commit has been sent, peers may have received it even if the call reports an error.
    if (pending->role == Role::Host && !pending->members.empty()) {
        pending->commit_send_failed = rpc(commit_method, pending->token) != OK;
    }
    apply_commit();
}

bool CrimsonSceneLoadManager::publish_world() {
    Node *container = get_world_container();
    if (!slot_is_available() || !get_staged_scene()) {
        return false;
    }
    const int64_t token = pending->token;
    const uint64_t replacement = pending->candidate_id;
    const uint64_t old_id = world_id;
    const StringName old_scene_id = current_scene_id;
    publishing_world = true;
    if (Node *old = get_current_scene()) {
        container->remove_child(old);
    }

    bool success = false;
    Node *world = get_staged_scene();
    container = get_world_container();
    if (pending && pending->token == token && world && container && slot_is_available()) {
        world->set_name(world_name);
        world_id = replacement;
        current_scene_id = pending->scene_id;
        container->add_child(world);
        world = get_current_scene();
        success = pending && pending->token == token && world && !world->is_queued_for_deletion();
    }
    if (success) {
        pending->candidate_id = 0;
        if (Node *old = find_node(old_id); old && !old->get_parent()) {
            old->queue_free();
        }
    } else {
        if (Node *failed = find_node(replacement); failed && failed->get_parent() == container && container) {
            container->remove_child(failed);
        }
        world_id = old_id;
        current_scene_id = old_scene_id;
        container = get_world_container();
        if (Node *old = find_node(old_id); container && old && !old->get_parent() && !old->is_queued_for_deletion()) {
            container->add_child(old);
        }
    }
    publishing_world = false;
    return success;
}

void CrimsonSceneLoadManager::apply_commit() {
    const int64_t token = pending->token;
    if (!publish_world()) {
        if (pending && pending->token == token) {
            finish_failure(ERR_CANT_CREATE, "The staged world could not be attached.");
        }
        return;
    }
    pending->phase = Phase::Applied;
    if (pending->role == Role::Host) {
        gate_has_committed_world = manage_admission;
        if (pending->commit_send_failed) {
            finish_failure(ERR_CONNECTION_ERROR, "The scene commit could not reach every peer.");
        } else if (pending->members.empty()) {
            finish_host_barrier();
        }
    } else if (pending->role == Role::Client) {
        if (rpc_id(server_id, applied_method, token, static_cast<int64_t>(OK)) != OK) {
            finish_failure(ERR_CONNECTION_ERROR, "The host could not confirm this peer's scene activation.");
        }
    } else {
        finish_success();
    }
}

void CrimsonSceneLoadManager::finish_host_barrier() {
    if (!pending->members.empty() && rpc(finalize_method, pending->token) != OK) {
        finish_failure(ERR_CONNECTION_ERROR, "The final scene confirmation could not reach every peer.");
        return;
    }
    gate_requires_recovery = false;
    finish_success();
}

void CrimsonSceneLoadManager::finish_success() {
    const StringName scene_id = pending->scene_id;
    pending.reset();
    emit_signal("load_completed", scene_id);
}

void CrimsonSceneLoadManager::discard_candidate(Transition &transition) {
    if (transition.resource_requested) {
        abandoned_loads.push_back(transition.resource_path);
    }
    if (Node *world = find_node(transition.candidate_id); world && !world->get_parent()) {
        world->queue_free();
    }
}

void CrimsonSceneLoadManager::finish_failure(Error error, const String &message, bool notify_remote) {
    Transition old = std::move(*pending);
    pending.reset();
    const bool after_commit = old.state_published || (old.role != Role::Offline &&
            (old.phase == Phase::CommitOrdered || old.phase == Phase::Applied));
    discard_candidate(old);
    const Ref<MultiplayerAPI> current = get_multiplayer();
    if (notify_remote && old.role != Role::Offline && current == old.network && current.is_valid() &&
            current->get_multiplayer_peer() == old.transport && is_transport_connected(old.transport)) {
        if (old.role == Role::Host && !get_members(current).empty()) {
            rpc(abort_method, old.token, static_cast<int64_t>(error), after_commit);
        } else if (old.role == Role::Client) {
            rpc_id(server_id, after_commit ? applied_method : ready_method, old.token, static_cast<int64_t>(error));
        }
    }
    if (old.role == Role::Host && manage_admission) {
        if (after_commit) {
            gate_requires_recovery = true;
        } else if (!gate_has_committed_world && !gate_requires_recovery) {
            release_admission_gate();
        }
    }
    emit_signal(after_commit ? "load_sync_failed" : "load_failed", old.scene_id, error, message);
}

Ref<MultiplayerAPI> CrimsonSceneLoadManager::incoming_server() const {
    if (leaving_tree || !is_inside_tree()) {
        return {};
    }
    const Ref<MultiplayerAPI> network = get_multiplayer();
    if (network.is_null() || network->get_remote_sender_id() != server_id || network->is_server() ||
            !is_transport_connected(network->get_multiplayer_peer())) {
        return {};
    }
    return network;
}

int64_t CrimsonSceneLoadManager::incoming_client(int64_t token) const {
    if (!pending || pending->role != Role::Host || token != pending->token || !is_inside_tree()) {
        return 0;
    }
    const Ref<MultiplayerAPI> network = get_multiplayer();
    if (network != pending->network || network.is_null() || !network->is_server() ||
            network->get_multiplayer_peer() != pending->transport) {
        return 0;
    }
    const int64_t sender = network->get_remote_sender_id();
    return pending->members.count(sender) != 0 ? sender : 0;
}

void CrimsonSceneLoadManager::rpc_prepare(int64_t token, const StringName &scene_id, int64_t timeout) {
    const Ref<MultiplayerAPI> network = incoming_server();
    if (network.is_null() || token <= 0 || !valid_scene_id(scene_id) || !valid_timeout(timeout)) {
        return;
    }
    const Ref<MultiplayerPeer> transport = network->get_multiplayer_peer();
    if (transport->get_instance_id() != last_server_transport_id) {
        last_server_transport_id = transport->get_instance_id();
        last_server_token = 0;
    }
    if (token <= last_server_token || pending) {
        return;
    }
    last_server_token = token;
    Transition transition;
    transition.role = Role::Client;
    transition.token = token;
    transition.scene_id = scene_id;
    transition.resource_path = scenes.get(scene_id, String());
    transition.network = network;
    transition.transport = transport;
    transition.timeout_msec = timeout;
    transition.deadline_msec = ticks_msec() + timeout + client_timeout_grace_msec;
    pending = std::move(transition);
    emit_signal("load_started", scene_id);
}

void CrimsonSceneLoadManager::rpc_ready(int64_t token, int64_t error) {
    const int64_t sender = incoming_client(token);
    if (!sender || (pending->phase != Phase::Preparing && pending->phase != Phase::Ready)) {
        return;
    }
    if (error != OK) {
        finish_failure(read_remote_error(error), "A peer could not prepare the requested scene.");
        return;
    }
    pending->ready_peers.insert(sender);
}

void CrimsonSceneLoadManager::rpc_commit(int64_t token) {
    const Ref<MultiplayerAPI> network = incoming_server();
    if (network.is_valid() && pending && pending->role == Role::Client && pending->token == token &&
            pending->network == network && network->get_multiplayer_peer() == pending->transport &&
            pending->phase == Phase::Ready) {
        pending->phase = Phase::CommitOrdered;
        pending->deadline_msec = ticks_msec() + pending->timeout_msec + client_timeout_grace_msec;
    }
}

void CrimsonSceneLoadManager::rpc_applied(int64_t token, int64_t error) {
    const int64_t sender = incoming_client(token);
    if (sender && (pending->phase == Phase::CommitOrdered || pending->phase == Phase::Applied)) {
        if (error == OK) {
            pending->applied_peers.insert(sender);
        } else {
            finish_failure(read_remote_error(error), "A peer could not activate the committed scene.");
        }
    }
}

void CrimsonSceneLoadManager::rpc_finalize(int64_t token) {
    const Ref<MultiplayerAPI> network = incoming_server();
    if (network.is_valid() && pending && pending->role == Role::Client && pending->token == token &&
            pending->network == network && network->get_multiplayer_peer() == pending->transport &&
            pending->phase == Phase::Applied) {
        if (get_current_scene()) {
            finish_success();
        } else {
            finish_failure(ERR_CANT_CREATE, "The committed world was removed before synchronization completed.");
        }
    }
}

void CrimsonSceneLoadManager::rpc_abort(int64_t token, int64_t error, bool after_commit) {
    const Ref<MultiplayerAPI> network = incoming_server();
    if (network.is_null() || !pending || pending->role != Role::Client || pending->token != token ||
            pending->network != network || network->get_multiplayer_peer() != pending->transport) {
        return;
    }
    if (after_commit && pending->phase == Phase::Ready) {
        pending->phase = Phase::CommitOrdered;
    }
    finish_failure(read_remote_error(error), "The host ended the scene transition.", false);
}

} // namespace crimson::scene_load
