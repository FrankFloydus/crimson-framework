// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#include "crimson_game.hpp"

#include <godot_cpp/classes/time.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/callable_method_pointer.hpp>

#include <limits>
#include <utility>

using namespace godot;

namespace crimson::integration {
namespace {

// A private view record included in every recipient's allowed projection.
constexpr const char *context_record = "crimson.context";

// Check a world or scene ID before catalog registration.
bool valid_world_key(const String &key) {
    if (key.is_empty() || key.length() > 64) {
        return false;
    }
    for (int64_t index = 0; index < key.length(); ++index) {
        const char32_t ch = key[index];
        const bool alnum = (ch >= U'a' && ch <= U'z') || (ch >= U'A' && ch <= U'Z') ||
                (ch >= U'0' && ch <= U'9');
        if (!alnum && (index == 0 || (ch != U'.' && ch != U'-' && ch != U'_'))) {
            return false;
        }
    }
    return true;
}

// Attach a persistent native child at the same RPC-relative name on every peer.
template <typename T>
T *add_service(Node &owner, const StringName &name) {
    T *service = memnew(T);
    service->set_name(name);
    owner.add_child(service);
    return service;
}

} // namespace

void CrimsonGame::_bind_methods() {
    ClassDB::bind_method(D_METHOD("set_definition", "definition"), &CrimsonGame::set_definition);
    ClassDB::bind_method(D_METHOD("get_definition"), &CrimsonGame::get_definition);
    ClassDB::bind_method(D_METHOD("start_offline"), &CrimsonGame::start_offline);
    ClassDB::bind_method(D_METHOD("open_offline"), &CrimsonGame::open_offline);
    ClassDB::bind_method(D_METHOD("host", "port", "max_clients", "bind_address"), &CrimsonGame::host,
            DEFVAL(32), DEFVAL(String("*")));
    ClassDB::bind_method(D_METHOD("join", "address", "port", "timeout_seconds"), &CrimsonGame::join, DEFVAL(10.0));
    ClassDB::bind_method(D_METHOD("leave"), &CrimsonGame::leave);
    ClassDB::bind_method(D_METHOD("start_match", "world"), &CrimsonGame::start_match, DEFVAL(StringName()));
    ClassDB::bind_method(D_METHOD("travel_to", "world"), &CrimsonGame::travel_to);
    ClassDB::bind_method(D_METHOD("save_slot", "profile", "slot"), &CrimsonGame::save_slot);
    ClassDB::bind_method(D_METHOD("save", "profile", "slot", "metadata", "autosave"), &CrimsonGame::save,
            DEFVAL(Dictionary()), DEFVAL(false));
    ClassDB::bind_method(D_METHOD("restore", "profile", "slot", "sequence", "generation_id"),
            &CrimsonGame::restore, DEFVAL(String()), DEFVAL(String()));
    ClassDB::bind_method(D_METHOD("save_history", "profile", "slot", "before_sequence", "before_id", "limit"),
            &CrimsonGame::save_history, DEFVAL(String()), DEFVAL(String()), DEFVAL(16));
    ClassDB::bind_method(D_METHOD("bind_player", "peer", "player_id"), &CrimsonGame::bind_player);
    ClassDB::bind_method(D_METHOD("register_command", "name", "handler", "requires_primary"),
            &CrimsonGame::register_command, DEFVAL(false));
    ClassDB::bind_method(D_METHOD("declare_command", "name", "requires_primary"),
            &CrimsonGame::declare_command, DEFVAL(false));
    ClassDB::bind_method(D_METHOD("get_local_player_id"), &CrimsonGame::get_local_player_id);
    ClassDB::bind_method(D_METHOD("get_local_character_id"), &CrimsonGame::get_local_character_id);
    ClassDB::bind_method(D_METHOD("submit", "name", "payload"), &CrimsonGame::submit, DEFVAL(Dictionary()));
    ClassDB::bind_method(D_METHOD("set_shared_stores", "store_ids"), &CrimsonGame::set_shared_stores);
    ClassDB::bind_method(D_METHOD("set_projector", "projector"), &CrimsonGame::set_projector);
    ClassDB::bind_method(D_METHOD("get_phase"), &CrimsonGame::get_phase);
    ClassDB::bind_method(D_METHOD("get_world_id"), &CrimsonGame::get_world_id);
    ClassDB::bind_method(D_METHOD("get_world_epoch"), &CrimsonGame::get_world_epoch);
    ClassDB::bind_method(D_METHOD("get_world"), &CrimsonGame::get_world);
    ClassDB::bind_method(D_METHOD("get_transactions"), &CrimsonGame::get_transactions);
    ClassDB::bind_method(D_METHOD("get_possession"), &CrimsonGame::get_possession);
    ClassDB::bind_method(D_METHOD("get_projection"), &CrimsonGame::get_projection);
    ClassDB::bind_method(D_METHOD("is_authority"), &CrimsonGame::is_authority);
    ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "definition", PROPERTY_HINT_RESOURCE_TYPE, "CrimsonGameDefinition"),
            "set_definition", "get_definition");
    ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "transactions"), "", "get_transactions");
    ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "possession"), "", "get_possession");
    ADD_SIGNAL(MethodInfo("phase_changed", PropertyInfo(Variant::INT, "phase")));
    ADD_SIGNAL(MethodInfo("world_ready", PropertyInfo(Variant::STRING_NAME, "world_id"),
            PropertyInfo(Variant::INT, "world_epoch")));
    ADD_SIGNAL(MethodInfo("peer_joined", PropertyInfo(Variant::INT, "peer_id")));
    ADD_SIGNAL(MethodInfo("peer_left", PropertyInfo(Variant::INT, "peer_id")));
    ADD_SIGNAL(MethodInfo("session_ended", PropertyInfo(Variant::STRING_NAME, "reason")));
    ADD_SIGNAL(MethodInfo("projection_updated", PropertyInfo(Variant::INT, "revision"),
            PropertyInfo(Variant::BOOL, "baseline")));
    BIND_ENUM_CONSTANT(IDLE);
    BIND_ENUM_CONSTANT(LOBBY);
    BIND_ENUM_CONSTANT(TRANSITIONING);
    BIND_ENUM_CONSTANT(RUNNING);
    BIND_ENUM_CONSTANT(RECOVERING);
}

void CrimsonGame::_ready() {
    if (session_node) {
        return;
    }
    world_container = add_service<Node>(*this, "WorldContainer");
    session_node = add_service<session::CrimsonGameSession>(*this, "Session");
    loader = add_service<scene_load::CrimsonSceneLoadManager>(*this, "SceneLoader");
    replication_node = add_service<replication::CrimsonReplication>(*this, "Replication");
    network = add_service<CrimsonMatchNetwork>(*this, "MatchNetwork");
    saves = add_service<checkpoint::SaveCoordinator>(*this, "Checkpoints");

    loader->set_world_container(world_container);
    loader->set_manage_admission(false);
    replication_node->set_projector(callable_mp(this, &CrimsonGame::project));
    network->set_dispatcher(callable_mp(this, &CrimsonGame::dispatch_remote));
    checkpoint_adapter.instantiate();
    reset_match();

    session_node->connect("session_started", callable_mp(this, &CrimsonGame::on_session_started));
    session_node->connect("session_ended", callable_mp(this, &CrimsonGame::on_session_ended));
    session_node->connect("peer_joined", callable_mp(this, &CrimsonGame::on_peer_joined));
    session_node->connect("peer_left", callable_mp(this, &CrimsonGame::on_peer_left));
    loader->connect("load_completed", callable_mp(this, &CrimsonGame::on_scene_completed));
    loader->connect("load_failed", callable_mp(this, &CrimsonGame::on_scene_failed));
    loader->connect("load_sync_failed", callable_mp(this, &CrimsonGame::on_scene_sync_failed));
    saves->connect("load_prepared", callable_mp(this, &CrimsonGame::on_load_prepared));
    saves->connect("operation_finished", callable_mp(this, &CrimsonGame::on_save_finished));
    network->connect("world_announced", callable_mp(this, &CrimsonGame::on_world_announced));
    network->connect("world_finished", callable_mp(this, &CrimsonGame::on_world_finished));
    replication_node->connect("peer_ready", callable_mp(this, &CrimsonGame::on_peer_ready));
    replication_node->connect("peer_sync_failed", callable_mp(this, &CrimsonGame::on_peer_sync_failed));
    replication_node->connect("projection_updated", callable_mp(this, &CrimsonGame::on_projection_updated));
    set_process_mode(PROCESS_MODE_ALWAYS);
    set_process(true);
}

void CrimsonGame::_exit_tree() {
    cleaning = true;
    if (network) {
        network->reset();
    }
    cancel_save_operations();
    if (transition.operation.is_valid()) {
        const Ref<CrimsonGameOperation> operation = transition.operation;
        transition = {};
        operation->finish(outcome(false, "Cancelled", "The game left the scene tree."));
    }
    phase = IDLE;
}

void CrimsonGame::_process(double) {
    if (cleaning) {
        cleanup_session();
        return;
    }
    if (pending_captures != 0) {
        capture_saves();
    }
    if (phase == TRANSITIONING && transition.deadline != 0 &&
            Time::get_singleton()->get_ticks_msec() >= transition.deadline) {
        finish_transition(false, "BaselineTimeout", "A participant did not acknowledge the world baseline.", true);
    }
    if (phase == RUNNING && is_authority() && session_node->get_state() == session::CrimsonGameSession::HOSTING) {
        const int64_t revision = transactions->get_revision();
        if (revision > replication_node->get_revision()) {
            replication_node->publish_changes(revision);
        }
    }
}

void CrimsonGame::set_definition(const Ref<CrimsonGameDefinition> &value) {
    if (!configured) {
        definition = value;
    }
}

Ref<CrimsonGameDefinition> CrimsonGame::get_definition() const {
    return definition;
}

Error CrimsonGame::configure_game() {
    if (cleaning) {
        return ERR_BUSY;
    }
    if (configured) {
        return OK;
    }
    if (!is_inside_tree() || !session_node || definition.is_null()) {
        return ERR_UNCONFIGURED;
    }
    const Dictionary catalog = definition->get_worlds();
    std::map<StringName, StringName> world_scenes;
    std::map<StringName, String> scene_paths;
    for (const Variant &key : catalog.keys()) {
        if (key.get_type() != Variant::STRING && key.get_type() != Variant::STRING_NAME) {
            return ERR_INVALID_PARAMETER;
        }
        const Variant entry_value = catalog[key];
        if (entry_value.get_type() != Variant::DICTIONARY || !valid_world_key(String(key))) {
            return ERR_INVALID_PARAMETER;
        }
        const Dictionary entry = entry_value;
        const Variant id = entry.get("scene_id", Variant());
        const Variant asset = entry.get("path", Variant());
        if ((id.get_type() != Variant::STRING && id.get_type() != Variant::STRING_NAME) ||
                asset.get_type() != Variant::STRING || !valid_world_key(String(id))) {
            return ERR_INVALID_PARAMETER;
        }
        const StringName scene_id = String(id);
        const String path = asset;
        if (!path.begins_with("res://") || path.length() > 512 || path.simplify_path() != path ||
                (!path.ends_with(".tscn") && !path.ends_with(".scn"))) {
            return ERR_INVALID_PARAMETER;
        }
        auto previous = scene_paths.find(scene_id);
        if ((previous != scene_paths.end() && previous->second != path) ||
                !world_scenes.emplace(StringName(String(key)), scene_id).second) {
            return ERR_INVALID_PARAMETER;
        }
        scene_paths.emplace(scene_id, path);
    }
    if (world_scenes.find(definition->get_starting_world()) == world_scenes.end() || definition->get_schema_version() <= 0) {
        return ERR_INVALID_PARAMETER;
    }
    if (!saves->configure(definition->get_app_id(), definition->get_save_root()) ||
            !saves->configure_scope("match", definition->get_schema_version()) ||
            !saves->set_host(checkpoint_adapter.ptr()) ||
            !saves->register_participant("match", definition->get_schema_version(), checkpoint_adapter.ptr(), true)) {
        return ERR_INVALID_PARAMETER;
    }
    for (const auto &[id, path] : scene_paths) {
        const Error error = loader->register_scene(id, path);
        if (error != OK) {
            return error;
        }
    }
    worlds = std::move(world_scenes);
    starting_world = definition->get_starting_world();
    configured = true;
    return OK;
}

void CrimsonGame::reset_match() {
    transactions.instantiate();
    possession.instantiate();
    bindings.instantiate();
    router.instantiate();
    world_id = {};
    world_epoch = 0;
    local_sequence = 0;
    remote_epoch = 0;
    remote_target = {};
    match_id = String();
    checkpoint_adapter->set_context(transactions, String());
}

void CrimsonGame::set_phase(Phase value) {
    if (phase != value) {
        phase = value;
        emit_signal("phase_changed", int64_t(value));
    }
}

Dictionary CrimsonGame::outcome(bool ok, const String &code, const String &message) const {
    Dictionary result;
    result["ok"] = ok;
    result["code"] = code;
    result["message"] = message;
    result["world_id"] = String(world_id);
    result["world_epoch"] = world_epoch;
    return result;
}

Ref<CrimsonGameOperation> CrimsonGame::completed(bool ok, const String &code, const String &message) const {
    Ref<CrimsonGameOperation> operation;
    operation.instantiate();
    operation->finish(outcome(ok, code, message));
    return operation;
}

Error CrimsonGame::open_offline() {
    const Error error = configure_game();
    if (error != OK) {
        return error;
    }
    const Error start = session_node->start_offline();
    if (start == OK && bindings->get_player(1).is_empty()) {
        return bind_player(1, "local");
    }
    return start;
}

Ref<CrimsonGameOperation> CrimsonGame::start_offline() {
    const Error error = open_offline();
    if (error != OK) {
        return completed(false, "OfflineStartRejected", "Could not open the offline lobby.");
    }
    return start_match();
}

Error CrimsonGame::host(int64_t port, int64_t max_clients, const String &bind_address) {
    const Error error = configure_game();
    return error == OK ? session_node->host(port, max_clients, bind_address) : error;
}

Error CrimsonGame::join(const String &address, int64_t port, double timeout_seconds) {
    const Error error = configure_game();
    return error == OK ? session_node->join(address, port, timeout_seconds) : error;
}

Error CrimsonGame::leave() {
    if (!session_node || cleaning || publishing) {
        return ERR_BUSY;
    }
    session_node->leave();
    return OK;
}

Ref<CrimsonGameOperation> CrimsonGame::start_match(const StringName &world) {
    if (phase != LOBBY || !is_authority()) {
        return completed(false, "NotInAuthorityLobby", "Start the match from the authority lobby.");
    }
    return travel_to(world.is_empty() ? starting_world : world);
}

Ref<CrimsonGameOperation> CrimsonGame::travel_to(const StringName &world) {
    if (worlds.find(world) == worlds.end()) {
        return completed(false, "UnknownWorld", "The requested world is absent from the game definition.");
    }
    Ref<CrimsonGameOperation> operation;
    operation.instantiate();
    const Error error = begin_transition(operation);
    if (error != OK) {
        operation->finish(outcome(false, "TransitionRejected", "The authority must be idle in its lobby or world with all peers bound."));
    } else {
        stage_scene(world);
    }
    return operation;
}

Error CrimsonGame::begin_transition(const Ref<CrimsonGameOperation> &operation) {
    if (!is_authority() || publishing || cleaning || (phase != LOBBY && phase != RUNNING) ||
            transition.operation.is_valid() || !peers_are_bound() || next_epoch == std::numeric_limits<int64_t>::max()) {
        return ERR_BUSY;
    }
    if (router->set_accepting_commands(false) != OK) {
        return ERR_BUSY;
    }
    capture_saves();
    transition.operation = operation;
    transition.previous = phase;
    transition.epoch = ++next_epoch;
    if (session_node->get_state() == session::CrimsonGameSession::HOSTING) {
        transition.reopen_lobby = session_node->is_accepting_connections();
        if (session_node->set_accepting_connections(false) != OK) {
            transition = {};
            router->set_accepting_commands(phase == RUNNING);
            return ERR_CONNECTION_ERROR;
        }
    }
    set_phase(TRANSITIONING);
    return OK;
}

void CrimsonGame::stage_scene(const StringName &target) {
    if (cleaning || transition.operation.is_null()) {
        return;
    }
    if (worlds.find(target) == worlds.end()) {
        finish_transition(false, "UnknownWorld", "The checkpoint references a world absent from the game definition.");
        return;
    }
    transition.target = target;
    const Error announce_error = network->announce_world(target, transition.epoch);
    transition.announced = true;
    if (announce_error != OK) {
        finish_transition(false, "WorldAnnouncementFailed", "Could not announce the destination to the session.");
        return;
    }
    const Error error = loader->request_coordinated_load(worlds.at(target), callable_mp(this, &CrimsonGame::publish_world));
    if (error != OK && transition.operation.is_valid()) {
        finish_transition(false, "SceneRejected", "The scene loader rejected the transition.");
    }
}

int64_t CrimsonGame::publish_world() {
    if (cleaning || transition.operation.is_null() || !is_authority()) {
        return ERR_UNAUTHORIZED;
    }
    if (transition.save_ticket != 0 && !saves->activate_prepared_load(transition.save_ticket)) {
        return ERR_INVALID_DATA;
    }
    transition.published = true;
    world_epoch = transition.epoch;
    world_id = transition.target;
    local_sequence = 0;
    checkpoint_adapter->set_context(transactions, String(world_id));
    // Keep teardown out of the incoming scene's _ready and deferred store notification.
    publishing = true;
    return OK;
}

void CrimsonGame::finish_transition(bool ok, const String &code, const String &message, bool recovery) {
    if (transition.operation.is_null()) {
        return;
    }
    Transition finished = std::exchange(transition, Transition{});
    publishing = false;
    if (!finished.published && finished.save_ticket != 0) {
        saves->cancel(finished.save_ticket);
    }
    if (finished.announced && is_authority() && network->finish_world(finished.epoch, ok, recovery) != OK &&
            finished.published) {
        ok = false;
        recovery = true;
    }
    if (!recovery && !ok && finished.reopen_lobby && is_authority()) {
        session_node->set_accepting_connections(true);
    }
    const Phase next = recovery ? RECOVERING : ok ? RUNNING : finished.previous;
    if (is_authority()) {
        router->set_accepting_commands(next == RUNNING);
    }
    Dictionary result = outcome(ok, recovery && code == "OK" ? String("WorldConfirmationFailed") : code, message);
    if (ok && !finished.generation.is_empty()) {
        result["value"] = finished.generation;
    }
    const int64_t ready_epoch = world_epoch;
    set_phase(next);
    if (ok && phase == RUNNING && world_epoch == ready_epoch && !cleaning) {
        emit_signal("world_ready", world_id, world_epoch);
    }
    finished.operation->finish(result);
}

Ref<CrimsonSaveSlot> CrimsonGame::save_slot(const String &profile, const String &slot) {
    Ref<CrimsonSaveSlot> handle;
    handle.instantiate();
    handle->configure(this, profile, slot);
    return handle;
}

Ref<CrimsonGameOperation> CrimsonGame::save(const String &profile, const String &slot,
        const Dictionary &metadata, bool autosave) {
    if (!is_authority() || phase != RUNNING || cleaning) {
        return completed(false, "SaveNotReady", "A world must be running on the authority to capture a save.");
    }
    const int64_t ticket = autosave ? saves->request_autosave(profile, slot, metadata) :
            saves->request_save(profile, slot, metadata);
    if (ticket == 0) {
        return completed(false, saves->get_last_admission_error(), "The checkpoint coordinator rejected the save.");
    }
    Ref<CrimsonGameOperation> operation;
    operation.instantiate();
    save_operations.emplace(ticket, operation);
    ++pending_captures;
    return operation;
}

Ref<CrimsonGameOperation> CrimsonGame::restore(const String &profile, const String &slot,
        const String &sequence, const String &generation_id) {
    Ref<CrimsonGameOperation> operation;
    operation.instantiate();
    if (begin_transition(operation) != OK) {
        operation->finish(outcome(false, "RestoreRejected", "Restore requires an authority lobby or ready world."));
        return operation;
    }
    if (cleaning || transition.operation != operation) {
        return operation;
    }
    transition.save_ticket = saves->request_prepare_load(profile, slot, sequence, generation_id);
    if (transition.save_ticket == 0) {
        finish_transition(false, saves->get_last_admission_error(), "The checkpoint coordinator rejected the restore.");
    }
    return operation;
}

Ref<CrimsonGameOperation> CrimsonGame::save_history(const String &profile, const String &slot,
        const String &before_sequence, const String &before_id, int64_t limit) {
    if (configure_game() != OK) {
        return completed(false, "Unconfigured", "Assign a valid game definition before reading save history.");
    }
    const int64_t ticket = saves->request_list(profile, slot, before_sequence, before_id, limit);
    if (ticket == 0) {
        return completed(false, saves->get_last_admission_error(), "The checkpoint coordinator rejected the read.");
    }
    Ref<CrimsonGameOperation> operation;
    operation.instantiate();
    save_operations.emplace(ticket, operation);
    return operation;
}

void CrimsonGame::capture_saves() {
    const int64_t count = std::exchange(pending_captures, int64_t(0));
    for (int64_t index = 0; index < count; ++index) {
        saves->capture_pending(String::num_int64(transactions->get_revision()));
    }
}

void CrimsonGame::on_load_prepared(int64_t ticket, const Dictionary &generation) {
    if (ticket == transition.save_ticket && transition.operation.is_valid() && !cleaning) {
        transition.generation = generation.duplicate(true);
        stage_scene(StringName(checkpoint_adapter->get_candidate_world(saves->get_prepared_candidate(ticket))));
    }
}

void CrimsonGame::on_save_finished(int64_t ticket, const Dictionary &result) {
    auto pending = save_operations.extract(ticket);
    if (!pending.empty()) {
        pending.mapped()->finish(result);
    } else if (transition.operation.is_valid() && ticket == transition.save_ticket &&
            !bool(result.get("ok", false)) && !transition.published && !cleaning) {
        finish_transition(false, result.get("code", "RestoreFailed"), result.get("message", ""));
    }
}

void CrimsonGame::cancel_save_operations() {
    pending_captures = 0;
    auto pending = std::exchange(save_operations, {});
    for (const auto &[ticket, operation] : pending) {
        saves->cancel(ticket);
        operation->finish(outcome(false, "SessionEnded", "The game stopped waiting for this storage operation."));
    }
}

void CrimsonGame::on_session_started(int64_t role, int64_t local_peer) {
    if (network->bind_transport() != OK) {
        session_node->leave();
        return;
    }
    if (role != session::CrimsonGameSession::CLIENT) {
        match_id = String("match.") + String::num_uint64(Time::get_singleton()->get_ticks_usec());
        if (router->configure(match_id, 1, possession) != OK) {
            session_node->leave();
            return;
        }
        for (const auto &[name, command] : commands) {
            if (command.handler.is_valid() &&
                    router->register_handler(name, command.handler, command.requires_primary) != OK) {
                session_node->leave();
                return;
            }
        }
        if (role == session::CrimsonGameSession::OFFLINE) {
            bindings->bind_peer(local_peer, "local");
        }
    }
    set_phase(LOBBY);
}

void CrimsonGame::on_session_ended(const StringName &reason) {
    cleaning = true;
    network->reset();
    router->set_accepting_commands(false);
    ended_reason = reason;
}

void CrimsonGame::cleanup_session() {
    if (loader->is_loading() || loader->clear_world() != OK || !saves->invalidate_world()) {
        return;
    }
    replication_node->reset();
    cancel_save_operations();
    if (transition.operation.is_valid()) {
        const auto operation = transition.operation;
        saves->cancel(transition.save_ticket);
        transition = {};
        operation->finish(outcome(false, "SessionEnded", "The session ended during the transition."));
    }
    publishing = false;
    checkpoint_adapter->discard_publication();
    reset_match();
    const StringName reason = std::exchange(ended_reason, StringName());
    cleaning = false;
    set_phase(IDLE);
    emit_signal("session_ended", reason);
}

void CrimsonGame::on_peer_joined(int64_t peer) {
    emit_signal("peer_joined", peer);
}

void CrimsonGame::on_peer_left(int64_t peer) {
    const int64_t generation = bindings->get_binding_generation(peer);
    const String player = bindings->get_player(peer);
    if (generation != 0) {
        bindings->unbind_peer(peer, generation);
        replication_node->remove_peer(peer, generation);
        router->reset_player_sequence(player);
    }
    if (!cleaning && transition.waiting.count(peer) != 0) {
        finish_transition(false, "PeerDisconnected", "A required participant disconnected during synchronization.", true);
    }
    emit_signal("peer_left", peer);
}

Error CrimsonGame::bind_player(int64_t peer, const String &player_id) {
    if (!is_authority() || phase != LOBBY || cleaning) {
        return ERR_UNAUTHORIZED;
    }
    if (peer != session_node->get_local_peer_id()) {
        const PackedInt32Array connected = session_node->get_connected_peer_ids();
        if (peer <= 0 || peer > std::numeric_limits<int32_t>::max() || !connected.has(int32_t(peer))) {
            return ERR_DOES_NOT_EXIST;
        }
    }
    const int64_t generation = bindings->bind_peer(peer, player_id);
    if (generation == 0) {
        return ERR_INVALID_PARAMETER;
    }
    if (peer != session_node->get_local_peer_id()) {
        const Error error = replication_node->register_peer(peer, player_id, generation);
        if (error != OK) {
            bindings->unbind_peer(peer, generation);
            return error;
        }
    }
    return OK;
}

bool CrimsonGame::peers_are_bound() const {
    for (const int32_t peer : session_node->get_connected_peer_ids()) {
        if (bindings->get_binding_generation(peer) == 0) {
            return false;
        }
    }
    return true;
}

void CrimsonGame::on_world_announced(const StringName &target, int64_t epoch) {
    if (cleaning || session_node->get_state() != session::CrimsonGameSession::CLIENT ||
            worlds.find(target) == worlds.end() || epoch <= world_epoch || remote_epoch != 0 ||
            (phase != LOBBY && phase != RUNNING)) {
        return;
    }
    remote_epoch = epoch;
    remote_target = target;
    remote_previous = phase;
    set_phase(TRANSITIONING);
}

void CrimsonGame::on_world_finished(int64_t epoch, bool ok, bool recovery) {
    if (cleaning || session_node->get_state() != session::CrimsonGameSession::CLIENT) {
        return;
    }
    if (epoch != remote_epoch || remote_epoch == 0) {
        if (!ok && recovery && epoch == world_epoch) {
            set_phase(RECOVERING);
        }
        return;
    }
    const StringName target = std::exchange(remote_target, StringName());
    remote_epoch = 0;
    if (!ok) {
        set_phase(recovery ? RECOVERING : remote_previous);
        return;
    }
    if (!replication_node->is_client_ready() || replication_node->get_world_epoch() != epoch ||
            loader->is_loading() || !loader->get_current_scene() || loader->get_current_scene_id() != worlds.at(target)) {
        set_phase(RECOVERING);
        return;
    }
    world_id = target;
    world_epoch = epoch;
    local_sequence = 0;
    set_phase(RUNNING);
    if (phase == RUNNING && !cleaning) {
        emit_signal("projection_updated", replication_node->get_revision(), true);
        emit_signal("world_ready", world_id, world_epoch);
    }
}

void CrimsonGame::on_scene_completed(const StringName &) {
    if (!is_authority() || cleaning || transition.operation.is_null()) {
        return;
    }
    const bool restored = transition.save_ticket != 0;
    if ((restored && checkpoint_adapter->notify_publication() != OK) ||
            router->advance_world(world_epoch) != OK) {
        finish_transition(false, "WorldPublicationFailed", "Could not finalize the committed state.", true);
        return;
    }
    publishing = false;
    if (session_node->get_state() == session::CrimsonGameSession::OFFLINE) {
        finish_transition(true, "OK");
        return;
    }
    const int64_t revision = transactions->get_revision();
    if (replication_node->advance_world(world_epoch, revision) != OK) {
        finish_transition(false, "ReplicationFailed", "Could not prepare the new replication stream.", true);
        return;
    }
    const PackedInt32Array peers = session_node->get_connected_peer_ids();
    for (const int32_t peer : peers) {
        transition.waiting.insert(peer);
    }
    if (transition.waiting.empty()) {
        finish_transition(true, "OK");
        return;
    }
    transition.deadline = Time::get_singleton()->get_ticks_msec() + loader->get_load_timeout_msec();
    for (const int32_t peer : peers) {
        if (replication_node->send_baseline(peer) != OK) {
            finish_transition(false, "BaselineFailed", "Could not send a participant's baseline.", true);
            return;
        }
    }
}

void CrimsonGame::on_scene_failed(const StringName &, int64_t, const String &message) {
    if (!cleaning && transition.operation.is_valid()) {
        finish_transition(false, "ScenePreparationFailed", message);
    }
}

void CrimsonGame::on_scene_sync_failed(const StringName &, int64_t, const String &message) {
    if (cleaning) {
        return;
    }
    if (transition.operation.is_valid()) {
        finish_transition(false, "SceneSynchronizationFailed", message, true);
    } else if (phase == TRANSITIONING) {
        set_phase(RECOVERING);
    }
}

void CrimsonGame::on_peer_ready(int64_t peer, int64_t epoch, int64_t generation) {
    if (!cleaning && transition.operation.is_valid() && transition.epoch == epoch &&
            generation == bindings->get_binding_generation(peer) && transition.waiting.erase(peer) != 0 &&
            transition.waiting.empty()) {
        finish_transition(true, "OK");
    }
}

void CrimsonGame::on_peer_sync_failed(int64_t peer, int64_t epoch, int64_t) {
    if (cleaning || epoch != world_epoch) {
        return;
    }
    if (transition.waiting.count(peer) != 0) {
        finish_transition(false, "BaselineFailed", "A required participant could not synchronize state.", true);
    } else if (phase == RUNNING && is_authority()) {
        router->set_accepting_commands(false);
        network->finish_world(world_epoch, false, true);
        set_phase(RECOVERING);
    }
}

void CrimsonGame::on_projection_updated(int64_t epoch, int64_t revision, bool baseline) {
    if (phase == RUNNING && epoch == world_epoch && !cleaning) {
        emit_signal("projection_updated", revision, baseline);
    }
}

Error CrimsonGame::declare_command(const String &name, bool requires_primary) {
    if (cleaning || (phase != IDLE && phase != LOBBY)) {
        return ERR_BUSY;
    }
    if (name.is_empty() || name.length() > 128 || name.strip_edges() != name) {
        return ERR_INVALID_PARAMETER;
    }
    const auto existing = commands.find(name);
    if (existing != commands.end()) {
        return existing->second.requires_primary == requires_primary ? OK : ERR_ALREADY_EXISTS;
    }
    commands.emplace(name, Command{Callable(), requires_primary});
    return OK;
}

Error CrimsonGame::register_command(const String &name, const Callable &handler, bool requires_primary) {
    if (!handler.is_valid()) {
        return ERR_INVALID_PARAMETER;
    }
    const Error declared = declare_command(name, requires_primary);
    if (declared != OK) {
        return declared;
    }
    Command &command = commands.at(name);
    if (command.handler.is_valid()) {
        return ERR_ALREADY_EXISTS;
    }
    if (is_authority()) {
        const Error installed = router->register_handler(name, handler, requires_primary);
        if (installed != OK) {
            return installed;
        }
    }
    command.handler = handler;
    return OK;
}
Error CrimsonGame::set_shared_stores(const PackedStringArray &store_ids) {
    if (phase != IDLE && phase != LOBBY) {
        return ERR_BUSY;
    }
    for (const String &store : store_ids) {
        if (store.is_empty() || store == context_record) {
            return ERR_INVALID_PARAMETER;
        }
    }
    shared_stores = store_ids;
    return OK;
}

Error CrimsonGame::set_projector(const Callable &projector) {
    if (phase != IDLE && phase != LOBBY) {
        return ERR_BUSY;
    }
    project_view = projector;
    return OK;
}

Dictionary CrimsonGame::project(int64_t peer, const String &player, int64_t epoch, int64_t revision) {
    Dictionary view;
    if (project_view.is_valid()) {
        const Variant projected = project_view.call(peer, player, epoch, revision);
        if (projected.get_type() != Variant::DICTIONARY) {
            return outcome(false, "InvalidProjection");
        }
        const Dictionary reply = projected;
        const Variant records = reply.get("view", Variant());
        if (!bool(reply.get("ok", false)) || records.get_type() != Variant::DICTIONARY) {
            return outcome(false, "InvalidProjection");
        }
        view = Dictionary(records).duplicate();
    } else {
        for (const String &store : shared_stores) {
            if (transactions->get_store_revision(store) < 0) {
                return outcome(false, "UnknownStore", store);
            }
            view[store] = transactions->read_store(store);
        }
    }
    Dictionary context;
    context["match_id"] = match_id;
    context["player_id"] = player;
    context["character_id"] = possession->get_primary(player);
    context["possession_revision"] = possession->get_revision(player);
    view[context_record] = context;
    Dictionary result;
    result["ok"] = true;
    result["view"] = view;
    return result;
}

Dictionary CrimsonGame::dispatch_remote(int64_t peer, const Dictionary &intent) {
    const int64_t generation = bindings->get_binding_generation(peer);
    if (!is_authority() || phase != RUNNING || cleaning || generation == 0 ||
            !replication_node->is_peer_ready(peer, generation)) {
        return outcome(false, "PeerNotReady", "The peer has no ready and admitted player binding.");
    }
    return router->submit(bindings->get_player(peer), intent);
}

Dictionary CrimsonGame::local_command_context() const {
    if (!is_authority()) {
        return replication_node->get_projection().get(context_record, Dictionary());
    }
    const String player = bindings->get_player(session_node->get_local_peer_id());
    Dictionary context;
    context["match_id"] = match_id;
    context["player_id"] = player;
    context["character_id"] = possession->get_primary(player);
    context["possession_revision"] = possession->get_revision(player);
    return context;
}

Ref<CrimsonGameOperation> CrimsonGame::submit(const String &name, const Dictionary &payload) {
    if (phase != RUNNING || cleaning || local_sequence == std::numeric_limits<int64_t>::max()) {
        return completed(false, "CommandsClosed", "The game must be ready before submitting commands.");
    }
    const auto declaration = commands.find(name);
    if (declaration == commands.end()) {
        return completed(false, "UnknownCommand", "Register this command on the game before submitting it.");
    }
    const Dictionary context = local_command_context();
    const String player = context.get("player_id", String());
    if (player.is_empty()) {
        return completed(false, "PlayerNotBound", "The local peer has no player binding.");
    }
    Dictionary intent;
    intent["type"] = name;
    intent["payload"] = payload;
    intent["match_id"] = context["match_id"];
    intent["world_epoch"] = world_epoch;
    intent["sequence"] = ++local_sequence;
    if (declaration->second.requires_primary) {
        const String character = context.get("character_id", String());
        if (character.is_empty()) {
            return completed(false, "NoPrimaryCharacter", "The command requires a possessed character.");
        }
        intent["character_id"] = character;
        intent["possession_revision"] = context["possession_revision"];
    }
    if (!is_authority()) {
        return network->submit_remote(intent);
    }
    Ref<CrimsonGameOperation> operation;
    operation.instantiate();
    operation->finish(router->submit(player, intent));
    return operation;
}

CrimsonGame::Phase CrimsonGame::get_phase() const {
    return phase;
}

StringName CrimsonGame::get_world_id() const {
    return world_id;
}

int64_t CrimsonGame::get_world_epoch() const {
    return world_epoch;
}

Node *CrimsonGame::get_world() const {
    return loader ? loader->get_current_scene() : nullptr;
}

Ref<match_runtime::CrimsonTransactionCoordinator> CrimsonGame::get_transactions() const {
    return session_node && (session_node->get_state() == session::CrimsonGameSession::CLIENT ||
            session_node->get_state() == session::CrimsonGameSession::CONNECTING) ?
            Ref<match_runtime::CrimsonTransactionCoordinator>() : transactions;
}

Ref<match_runtime::CrimsonPossession> CrimsonGame::get_possession() const {
    return get_transactions().is_valid() ? possession : Ref<match_runtime::CrimsonPossession>();
}

String CrimsonGame::get_local_player_id() const {
    if (!session_node || phase == IDLE || cleaning) {
        return {};
    }
    return local_command_context().get("player_id", String());
}

String CrimsonGame::get_local_character_id() const {
    if (phase != RUNNING || cleaning) {
        return {};
    }
    return local_command_context().get("character_id", String());
}

Dictionary CrimsonGame::get_projection() const {
    Dictionary view = phase == RUNNING && replication_node ? replication_node->get_projection() : Dictionary();
    view.erase(context_record);
    return view;
}

bool CrimsonGame::is_authority() const {
    return session_node && session_node->is_authority();
}

} // namespace crimson::integration
