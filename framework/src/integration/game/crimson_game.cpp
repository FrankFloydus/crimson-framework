// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#include "integration/game/crimson_game.hpp"
#include "integration/online/steam/steam_online.hpp"
#include "integration/game/game_services.hpp"
#include "integration/game/game_voice.hpp"
#include "integration/game/multiplayer_rules.hpp"
#include "integration/online/steam/steam_dedicated.hpp"

#include <godot_cpp/classes/time.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/callable_method_pointer.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

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

void GameRuntime::_bind_methods() {
    ClassDB::bind_static_method(get_class_static(), D_METHOD("configure"), &GameRuntime::configure);
    ClassDB::bind_method(D_METHOD("set_definition", "definition"), &GameRuntime::set_definition);
    ClassDB::bind_method(D_METHOD("get_definition"), &GameRuntime::get_definition);
    ClassDB::bind_method(D_METHOD("start_offline"), &GameRuntime::start_offline);
    ClassDB::bind_method(D_METHOD("play_solo"), &GameRuntime::play_solo);
    ClassDB::bind_method(D_METHOD("host_game", "bind_address"), &GameRuntime::host_game, DEFVAL(String("*")));
    ClassDB::bind_method(D_METHOD("start_dedicated", "bind_address"), &GameRuntime::start_dedicated, DEFVAL(String("*")));
    ClassDB::bind_method(D_METHOD("join_game", "address"), &GameRuntime::join_game);
    ClassDB::bind_method(D_METHOD("host_online", "public_address", "bind_address"), &GameRuntime::host_online,
            DEFVAL(String()), DEFVAL(String("*")));
    ClassDB::bind_method(D_METHOD("start_dedicated_online", "public_address", "bind_address"),
            &GameRuntime::start_dedicated_online, DEFVAL(String()), DEFVAL(String("*")));
    ClassDB::bind_method(D_METHOD("join_online", "listing_id"), &GameRuntime::join_online);
    ClassDB::bind_method(D_METHOD("join_listing", "listing"), &GameRuntime::join_listing);
    ClassDB::bind_method(D_METHOD("invites"), &GameRuntime::invites);
    ClassDB::bind_method(D_METHOD("voice"), &GameRuntime::voice);
    ClassDB::bind_method(D_METHOD("get_online_listing"), &GameRuntime::get_online_listing);
    ClassDB::bind_method(D_METHOD("open_offline"), &GameRuntime::open_offline);
    ClassDB::bind_method(D_METHOD("host", "port", "max_clients", "bind_address"), &GameRuntime::host,
            DEFVAL(32), DEFVAL(String("*")));
    ClassDB::bind_method(D_METHOD("join", "address", "port", "timeout_seconds"), &GameRuntime::join, DEFVAL(10.0));
    ClassDB::bind_method(D_METHOD("leave"), &GameRuntime::leave);
    ClassDB::bind_method(D_METHOD("start_match", "world"), &GameRuntime::start_match, DEFVAL(StringName()));
    ClassDB::bind_method(D_METHOD("travel_to", "world"), &GameRuntime::travel_to);
    ClassDB::bind_method(D_METHOD("save_slot", "profile", "slot"), &GameRuntime::save_slot);
    ClassDB::bind_method(D_METHOD("save", "profile", "slot", "metadata", "autosave"), &GameRuntime::save,
            DEFVAL(Dictionary()), DEFVAL(false));
    ClassDB::bind_method(D_METHOD("restore", "profile", "slot", "sequence", "generation_id"),
            &GameRuntime::restore, DEFVAL(String()), DEFVAL(String()));
    ClassDB::bind_method(D_METHOD("save_history", "profile", "slot", "before_sequence", "before_id", "limit"),
            &GameRuntime::save_history, DEFVAL(String()), DEFVAL(String()), DEFVAL(16));
    ClassDB::bind_method(D_METHOD("bind_player", "peer", "player_id"), &GameRuntime::bind_player);
    ClassDB::bind_method(D_METHOD("register_command", "name", "handler", "requires_primary"),
            &GameRuntime::register_command, DEFVAL(false));
    ClassDB::bind_method(D_METHOD("declare_command", "name", "requires_primary"),
            &GameRuntime::declare_command, DEFVAL(false));
    ClassDB::bind_method(D_METHOD("get_local_player_id"), &GameRuntime::get_local_player_id);
    ClassDB::bind_method(D_METHOD("get_local_character_id"), &GameRuntime::get_local_character_id);
    ClassDB::bind_method(D_METHOD("submit", "name", "payload"), &GameRuntime::submit, DEFVAL(Dictionary()));
    ClassDB::bind_method(D_METHOD("set_shared_stores", "store_ids"), &GameRuntime::set_shared_stores);
    ClassDB::bind_method(D_METHOD("set_projector", "projector"), &GameRuntime::set_projector);
    ClassDB::bind_method(D_METHOD("get_phase"), &GameRuntime::get_phase);
    ClassDB::bind_method(D_METHOD("get_world_id"), &GameRuntime::get_world_id);
    ClassDB::bind_method(D_METHOD("get_world_epoch"), &GameRuntime::get_world_epoch);
    ClassDB::bind_method(D_METHOD("get_world"), &GameRuntime::get_world);
    ClassDB::bind_method(D_METHOD("get_local_player"), &GameRuntime::get_local_player);
    ClassDB::bind_method(D_METHOD("get_player", "peer_id"), &GameRuntime::get_player);
    ClassDB::bind_method(D_METHOD("get_transactions"), &GameRuntime::get_transactions);
    ClassDB::bind_method(D_METHOD("get_possession"), &GameRuntime::get_possession);
    ClassDB::bind_method(D_METHOD("get_projection"), &GameRuntime::get_projection);
    ClassDB::bind_method(D_METHOD("is_authority"), &GameRuntime::is_authority);
    ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "definition", PROPERTY_HINT_RESOURCE_TYPE, "GameDefinition"),
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

void GameRuntime::_enter_tree() {
    if (session_node || !service_setup_error.is_empty()) {
        request_ready();
    }
}

void GameRuntime::_ready() {
    if (code_first_retired) {
        return;
    }
    if (session_node) {
        GameServiceContainer::open(*this);
        online_directory = GameServiceContainer::make_online_directory(*this, *session_node);
        republish_after_cleanup = true;
        cleanup_session();
        return;
    }
    cleaning = false;
    service_setup_error = String();
    GameServiceContainer::open(*this);
    session_node = GameServiceContainer::make_session(*this);
    loader = GameServiceContainer::make_scene_loader(*this);
    if (!session_node || !loader) {
        if (!session_node && !loader) {
            service_setup_error = "The game service provider must return a fresh detached GameSession and SceneLoadManager.";
        } else if (!session_node) {
            service_setup_error = "The game service provider must return a fresh detached GameSession.";
        } else {
            service_setup_error = "The game service provider must return a fresh detached SceneLoadManager.";
        }
        if (session_node) {
            memdelete(session_node);
            session_node = nullptr;
        }
        if (loader) {
            memdelete(loader);
            loader = nullptr;
        }
        GameServiceContainer::close(*this);
        if (!code_first_owned) {
            UtilityFunctions::printerr("[Crimson] Game services could not initialize. ", service_setup_error);
        }
        return;
    }
    world_container = add_service<Node>(*this, "WorldContainer");
    session_node->set_name("Session");
    add_child(session_node);
    loader->set_name("SceneLoader");
    add_child(loader);
    replication_node = add_service<replication::StateReplication>(*this, "Replication");
    network = add_service<MatchNetwork>(*this, "MatchNetwork");
    voice_service = add_service<GameVoice>(*this, "Voice");
    saves = add_service<checkpoint::SaveCoordinator>(*this, "Checkpoints");

    loader->set_world_container(world_container);
    loader->set_manage_admission(false);
    replication_node->set_projector(callable_mp(this, &GameRuntime::project));
    network->set_dispatcher(callable_mp(this, &GameRuntime::dispatch_remote));
    checkpoint_adapter.instantiate();
    reset_match();

    session_node->connect("session_started", callable_mp(this, &GameRuntime::on_session_started));
    session_node->connect("session_ended", callable_mp(this, &GameRuntime::on_session_ended));
    session_node->connect("peer_joined", callable_mp(this, &GameRuntime::on_peer_joined));
    session_node->connect("peer_left", callable_mp(this, &GameRuntime::on_peer_left));
    loader->connect("load_completed", callable_mp(this, &GameRuntime::on_scene_completed));
    loader->connect("load_failed", callable_mp(this, &GameRuntime::on_scene_failed));
    loader->connect("load_sync_failed", callable_mp(this, &GameRuntime::on_scene_sync_failed));
    saves->connect("load_prepared", callable_mp(this, &GameRuntime::on_load_prepared));
    saves->connect("operation_finished", callable_mp(this, &GameRuntime::on_save_finished));
    network->connect("world_announced", callable_mp(this, &GameRuntime::on_world_announced));
    network->connect("world_finished", callable_mp(this, &GameRuntime::on_world_finished));
    replication_node->connect("peer_ready", callable_mp(this, &GameRuntime::on_peer_ready));
    replication_node->connect("peer_sync_failed", callable_mp(this, &GameRuntime::on_peer_sync_failed));
    replication_node->connect("projection_updated", callable_mp(this, &GameRuntime::on_projection_updated));
    set_process_mode(PROCESS_MODE_ALWAYS);
    set_process(true);
    publishing = true;
    online_directory = GameServiceContainer::make_online_directory(*this, *session_node);
    publishing = false;
    GameServiceContainer::publish(*this);
}

GameVoice *GameRuntime::voice() const {
    if (!voice_service || !is_inside_tree() || cleaning) {
        return nullptr;
    }
    auto *owner = const_cast<GameRuntime *>(this);
    const auto steam = SteamServices::for_game(owner);
    const auto server = SteamServices::server_for(owner);
    return (steam.is_valid() && steam->voice().is_valid()) || server.is_valid() ? voice_service : nullptr;
}

void GameRuntime::_exit_tree() {
    code_first_retired = code_first_owned;
    cleaning = true;
    republish_after_cleanup = false;
    GameServiceContainer::close(*this);
    if (network) {
        network->reset();
    }
    cancel_save_operations();
    if (transition.operation.is_valid()) {
        const Ref<GameOperation> operation = transition.operation;
        transition = {};
        operation->finish(outcome(false, "Cancelled", "The game left the scene tree."));
    }
    report_remote_travel(false, "Cancelled", "The game left the scene tree before world travel completed.");
    if (immediate_start.is_valid()) {
        const auto operation = std::exchange(immediate_start, {});
        operation->finish(outcome(false, "Cancelled", "The game left the scene tree."));
    }
    const auto online = retire_online_start();
    retire_online_presence();
    online_directory.unref();
    if (online.is_valid()) {
        online->finish(outcome(false, "Cancelled", "The game left the scene tree."));
    }
    immediate_active = false;
    immediate_world_id = StringName();
    phase = IDLE;
}

void GameRuntime::_process(double) {
    if (cleaning) {
        cleanup_session();
        return;
    }
    if (online_start.operation.is_valid()) {
        process_online_start();
        // Completing online startup can resume a caller that frees this game.
        return;
    }
    if (pending_captures != 0) {
        capture_saves();
    }
    if (phase == TRANSITIONING && transition.deadline != 0 &&
            Time::get_singleton()->get_ticks_msec() >= transition.deadline) {
        finish_transition(false, "BaselineTimeout", "A participant did not acknowledge the world baseline.", true);
        return;
    }
    if (!immediate_active && phase == RUNNING && is_authority() &&
            session_node->get_state() == session::GameSession::HOSTING) {
        const int64_t revision = transactions->get_revision();
        if (revision > replication_node->get_revision()) {
            const int64_t publishing_epoch = world_epoch;
            const Error error = replication_node->publish_changes(revision);
            if (error != OK && phase == RUNNING && world_epoch == publishing_epoch && !cleaning && is_authority()) {
                recover_replication("Could not publish committed state: " + UtilityFunctions::error_string(error) + String("."));
            }
        }
    }
}

void GameRuntime::set_definition(const Ref<GameDefinition> &value) {
    if (!configured && !immediate_active && !publishing && !online_start.operation.is_valid()) {
        definition = value;
    }
}

Ref<GameDefinition> GameRuntime::configure() {
    return GameDefinition::configure();
}

Ref<GameDefinition> GameRuntime::get_definition() const {
    return definition;
}

Error GameRuntime::configure_game() {
    if (cleaning || immediate_active || publishing || online_start.operation.is_valid()) {
        return ERR_BUSY;
    }
    if (!is_inside_tree() || !session_node || !loader || definition.is_null()) {
        return ERR_UNCONFIGURED;
    }
    if (loader->get_world_container() != world_container) {
        const Error mount = loader->set_world_container(world_container);
        if (mount != OK) {
            return mount;
        }
        loader->set_coordinated_enabled(true);
    }
    if (configured) {
        return OK;
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

void GameRuntime::reset_match() {
    transactions.instantiate();
    possession.instantiate();
    bindings.instantiate();
    router.instantiate();
    world_id = {};
    world_epoch = 0;
    local_sequence = 0;
    remote_epoch = 0;
    remote_target = {};
    remote_outcome_reported = false;
    remote_preparation_error = String();
    rejected_remote_epoch = 0;
    match_id = String();
    checkpoint_adapter->set_context(transactions, String());
}

void GameRuntime::set_phase(Phase value) {
    if (phase != value) {
        phase = value;
        emit_signal("phase_changed", int64_t(value));
    }
}

Dictionary GameRuntime::outcome(bool ok, const String &code, const String &message) const {
    Dictionary result;
    result["ok"] = ok;
    result["code"] = code;
    result["message"] = message;
    result["world_id"] = String(world_id);
    result["world_epoch"] = world_epoch;
    return result;
}

Ref<GameOperation> GameRuntime::completed(bool ok, const String &code, const String &message) const {
    Ref<GameOperation> operation;
    operation.instantiate();
    operation->finish(outcome(ok, code, message));
    return operation;
}

Error GameRuntime::open_offline() {
    const Error error = configure_game();
    if (error != OK) {
        if (service_setup_error.is_empty()) {
            UtilityFunctions::printerr("[Crimson] Could not configure the offline lobby (",
                    UtilityFunctions::error_string(error), "). Check the game definition and current lifecycle phase.");
        }
        return error;
    }
    const Error start = session_node->start_offline();
    if (start == OK && bindings->get_player(1).is_empty()) {
        const Error binding = bind_player(1, "local");
        if (binding != OK) {
            UtilityFunctions::printerr("[Crimson] Could not establish the offline player's binding (",
                    UtilityFunctions::error_string(binding), ").");
        }
        return binding;
    }
    return start;
}

Ref<GameOperation> GameRuntime::start_offline() {
    const Error error = open_offline();
    if (error != OK) {
        return completed(false, "OfflineStartRejected",
                service_setup_error.is_empty() ?
                        "Could not open the offline lobby: " + UtilityFunctions::error_string(error) + String(".") :
                        service_setup_error);
    }
    return start_match();
}

Ref<GameOperation> GameRuntime::play_solo() {
    return start_immediate(ImmediateMode::Solo);
}

Ref<GameOperation> GameRuntime::host_game(const String &bind_address) {
    return start_immediate(ImmediateMode::Host, bind_address.strip_edges());
}

Ref<GameOperation> GameRuntime::start_dedicated(const String &bind_address) {
    return start_immediate(ImmediateMode::Dedicated, bind_address.strip_edges());
}

Ref<GameOperation> GameRuntime::join_game(const String &address) {
    return start_immediate(ImmediateMode::Join, address.strip_edges());
}

Ref<GameOperation> GameRuntime::start_immediate(ImmediateMode mode, const String &address,
        const Ref<session::SessionEndpoint> &endpoint, bool online) {
    if (!service_setup_error.is_empty()) {
        return completed(false, "ServiceInitializationFailed", service_setup_error);
    }
    if (!session_node || !is_inside_tree() || definition.is_null()) {
        return completed(false, "Unconfigured", "Assign a game definition before starting.");
    }
    if (phase != IDLE || immediate_active || cleaning || publishing ||
            (!online && online_start.operation.is_valid()) ||
            session_node->get_state() != session::GameSession::IDLE) {
        return completed(false, "GameBusy", "Leave the current game before starting another.");
    }
    if (online && (online_start.authenticator.is_null() ||
            session_node->get_authenticator() != online_start.authenticator)) {
        return completed(false, "OnlineAuthenticationRequired", "The selected peer authenticator changed before startup.");
    }
    const Ref<PackedScene> world = definition->get_starting_world_scene();
    const Ref<PackedScene> player = definition->get_player_scene();
    const StringName target = definition->get_starting_world();
    if (world.is_null() || player.is_null() || !valid_world_key(String(target))) {
        return completed(false, "Unconfigured", "Assign a starting world and a player scene.");
    }
    const Ref<MultiplayerRules> rules = definition->get_multiplayer_rules();
    if (mode != ImmediateMode::Solo) {
        if (rules.is_null()) {
            return completed(false, "MultiplayerDisabled", "Configure multiplayer rules before hosting or joining.");
        }
        if (!rules->get_join_in_progress()) {
            return completed(false, "LobbyRequired", "Use the coordinated host, join, and start_match actions for lobby-only admission.");
        }
        const int64_t minimum = mode == ImmediateMode::Host ? 2 : 1;
        if (rules->get_max_players() < minimum || rules->get_max_players() > WorldSession::MAX_PLAYERS ||
                rules->get_port() < 1 || rules->get_port() > 65535) {
            return completed(false, "InvalidMultiplayerRules", "Choose a valid player limit and UDP port.");
        }
        if (mode == ImmediateMode::Join && endpoint.is_null() && address.is_empty()) {
            return completed(false, "InvalidAddress", "Enter a host address.");
        }
        if ((mode == ImmediateMode::Host || mode == ImmediateMode::Dedicated) && address != "*" &&
                !address.is_valid_ip_address()) {
            return completed(false, "InvalidAddress", "Use a literal bind IP address or *.");
        }
    }
    if (mode != ImmediateMode::Join && next_epoch == (std::numeric_limits<int64_t>::max)()) {
        return completed(false, "EpochExhausted", "The game cannot allocate another world epoch.");
    }
    immediate_active = true;
    immediate_mode = mode;
    if (!immediate) {
        immediate = memnew(WorldSession);
        immediate->set_name("Immediate");
        immediate->use_services(session_node, loader);
        add_child(immediate);
        immediate->connect("state_changed", callable_mp(this, &GameRuntime::on_immediate_state));
        immediate->connect("session_ended", callable_mp(this, &GameRuntime::on_immediate_ended));
    }
    immediate->configure_immediate(world, player, target, mode == ImmediateMode::Solo ? 1 : rules->get_max_players(),
            mode == ImmediateMode::Solo ? WorldSessionDefinition::DEFAULT_PORT : rules->get_port(),
            mode == ImmediateMode::Join ? 0 : ++next_epoch);
    immediate->configure_movement(player_movement);
    Ref<GameOperation> operation;
    operation.instantiate();
    immediate_start = operation;
    immediate_world_id = target;
    Error error = OK;
    switch (mode) {
        case ImmediateMode::Solo:
            error = immediate->start_offline();
            break;
        case ImmediateMode::Host:
            error = immediate->host_immediate(address, false, online);
            break;
        case ImmediateMode::Join:
            error = endpoint.is_valid() ? immediate->join_endpoint(endpoint) : immediate->join(address);
            break;
        case ImmediateMode::Dedicated:
            error = immediate->host_immediate(address, true, online);
            break;
    }
    if (error != OK) {
        immediate_start.unref();
        immediate_world_id = StringName();
        immediate_active = false;
        const bool invalid_scene = error == ERR_INVALID_DATA || error == ERR_UNCONFIGURED;
        const String message = invalid_scene ?
                "Check the world spawn points and player scene (" + UtilityFunctions::error_string(error) + String(").") :
                "Could not start the game session (" + UtilityFunctions::error_string(error) + String(").");
        Dictionary result = outcome(false, invalid_scene ? "InvalidSpawnSetup" : "StartRejected", message);
        result["error"] = int64_t(error);
        operation->finish(result);
    }
    return operation;
}

void GameRuntime::on_immediate_state(int64_t state) {
    if (!immediate_active || !immediate_start.is_valid()) {
        return;
    }
    if (state == WorldSession::STARTING) {
        set_phase(TRANSITIONING);
    } else if (state == WorldSession::RUNNING) {
        const auto operation = std::exchange(immediate_start, {});
        world_id = immediate_world_id;
        world_epoch = immediate->get_world_epoch();
        if (online_start.operation.is_valid()) {
            operation->finish(outcome(true, "Ok"));
            return;
        }
        const int64_t ready_epoch = world_epoch;
        const Dictionary result = outcome(true, "Ok");
        set_phase(RUNNING);
        if (immediate_active && phase == RUNNING && world_epoch == ready_epoch) {
            emit_signal("world_ready", world_id, world_epoch);
        }
        operation->finish(result);
    }
}

void GameRuntime::on_immediate_ended(const StringName &reason) {
    if (!immediate_active) {
        return;
    }
    const auto operation = std::exchange(immediate_start, {});
    immediate_active = false;
    immediate_world_id = StringName();
    world_id = StringName();
    world_epoch = 0;
    Dictionary result;
    if (operation.is_valid()) {
        const bool joining = immediate_mode == ImmediateMode::Join;
        String code = joining ? "JoinFailed" : "StartFailed";
        String message = joining ? "Could not join the game." : "Could not start the game.";
        if (reason == StringName("left")) {
            code = "Cancelled";
            message = joining ? "Join cancelled." : "Startup cancelled.";
        } else if (reason == StringName("spawn_unavailable")) {
            code = "SpawnUnavailable";
            message = "The host has no available spawn placement.";
        } else if (reason == StringName("incompatible_world")) {
            code = "IncompatibleWorld";
            message = "The host uses a different starting world or spawn layout.";
        } else if (reason == StringName("incompatible_rules")) {
            code = "IncompatibleRules";
            message = "The host uses different multiplayer rules.";
        } else if (reason == StringName("connection_timeout")) {
            message = "The host did not respond.";
        } else if (reason == StringName("spawn_timeout")) {
            message = "The host did not provide a spawn assignment in time.";
        } else if (reason == StringName("join_timeout")) {
            message = "The guest did not finish loading or applying the baseline in time.";
        } else if (reason == StringName("invalid_spawn_setup")) {
            code = "InvalidSpawnSetup";
            message = "Check the world spawn points and player scene.";
        } else if (reason == StringName("scene_load_failed")) {
            code = "SceneLoadFailed";
            message = "The starting world could not be loaded.";
        }
        const String detail = immediate->get_last_end_detail();
        if (!detail.is_empty()) {
            message += String(" ") + detail;
            if (immediate_mode != ImmediateMode::Dedicated) {
                UtilityFunctions::printerr("[Crimson] ", message);
            }
        }
        result = outcome(false, code, message);
        result["reason"] = String(reason);
    }
    publishing = true;
    const auto online = retire_online_start();
    retire_online_presence();
    set_phase(IDLE);
    publishing = false;
    emit_signal("session_ended", reason);
    if (operation.is_valid()) {
        operation->finish(result);
    }
    if (online.is_valid()) {
        if (result.is_empty()) {
            result = reason == StringName("left") ?
                    outcome(false, "Cancelled", "Online startup cancelled.") :
                    outcome(false, "StartFailed", "The session ended before online startup completed.");
            result["reason"] = String(reason);
        }
        online->finish(result);
    }
}

Error GameRuntime::host(int64_t port, int64_t max_clients, const String &bind_address) {
    const Error error = configure_game();
    return error == OK ? session_node->host(port, max_clients, bind_address) : error;
}

Error GameRuntime::join(const String &address, int64_t port, double timeout_seconds) {
    const Error error = configure_game();
    return error == OK ? session_node->join(address, port, timeout_seconds) : error;
}

Error GameRuntime::leave() {
    if (!session_node || cleaning || publishing) {
        return ERR_BUSY;
    }
    if (immediate_active) {
        return immediate->leave();
    }
    if (online_start.operation.is_valid()) {
        publishing = true;
        const auto online = retire_online_start();
        retire_online_presence();
        publishing = false;
        online->finish(outcome(false, "Cancelled", "Online startup cancelled."));
        return OK;
    }
    session_node->leave();
    return OK;
}

Ref<GameOperation> GameRuntime::start_match(const StringName &world) {
    const StringName target = world.is_empty() ? starting_world : world;
    if (phase != LOBBY || !is_authority()) {
        const auto operation = GameOperation::for_travel(target);
        operation->finish(outcome(false, "NotInAuthorityLobby", "Start the match from the authority lobby."));
        return operation;
    }
    return travel_to(target);
}

Ref<GameOperation> GameRuntime::travel_to(const StringName &world) {
    const auto operation = GameOperation::for_travel(world);
    if (immediate_active) {
        operation->finish(outcome(false, "CoordinatedGameRequired", "Use the coordinated game flow for world travel."));
        return operation;
    }
    if (worlds.find(world) == worlds.end()) {
        const String target(world);
        operation->finish(outcome(false, "UnknownWorld", valid_world_key(target) ?
                "World '" + target + "' is absent from the game definition." :
                String("The requested world ID is invalid. Use a world ID from the game definition.")));
        return operation;
    }
    const String rejection = begin_transition(operation);
    if (!rejection.is_empty()) {
        operation->finish(outcome(false, "TransitionRejected", rejection));
    } else if (!cleaning && transition.operation == operation) {
        stage_scene(world);
    }
    return operation;
}

String GameRuntime::begin_transition(const Ref<GameOperation> &operation) {
    if (immediate_active) {
        return "Use the coordinated game flow for world travel and checkpoint restore.";
    }
    if (!is_authority()) {
        return "Start the transition from the game authority.";
    }
    if (publishing || cleaning || transition.operation.is_valid()) {
        return "A world transition or session cleanup is in progress. Wait for it to finish before retrying.";
    }
    if (phase != LOBBY && phase != RUNNING) {
        return phase == RECOVERING ? "The session is recovering. Reopen the session before starting another transition." :
                                    "Open an authority lobby or wait for the current world to become ready.";
    }
    if (!peers_are_bound()) {
        return "Bind every admitted participant to a trusted player ID before starting a transition.";
    }
    if (next_epoch == std::numeric_limits<int64_t>::max()) {
        return "This game instance has exhausted its world epochs. Start a new game instance.";
    }
    const Error command_gate = router->set_accepting_commands(false);
    if (command_gate != OK) {
        return "Could not close gameplay command admission: " + UtilityFunctions::error_string(command_gate) + ".";
    }
    transition.operation = operation;
    transition.previous = phase;
    transition.epoch = ++next_epoch;
    capture_saves();
    // Checkpoint capture can call game code that retires the session.
    if (cleaning || transition.operation != operation) {
        return {};
    }
    if (session_node->get_state() == session::GameSession::HOSTING) {
        transition.reopen_lobby = session_node->is_accepting_connections();
        const Error admission_gate = session_node->set_accepting_connections(false);
        if (cleaning || transition.operation != operation) {
            return {};
        }
        if (admission_gate != OK) {
            transition = {};
            router->set_accepting_commands(phase == RUNNING);
            return "Could not close multiplayer connection admission: " + UtilityFunctions::error_string(admission_gate) + ".";
        }
    }
    set_phase(TRANSITIONING);
    return {};
}

void GameRuntime::stage_scene(const StringName &target) {
    if (cleaning || transition.operation.is_null()) {
        return;
    }
    const auto destination = worlds.find(target);
    if (destination == worlds.end()) {
        const String key(target);
        finish_transition(false, "UnknownWorld", valid_world_key(key) ?
                "The checkpoint references world '" + key + "', which is absent from the game definition." :
                String("The checkpoint does not identify a valid world in the game definition."));
        return;
    }
    const auto operation = transition.operation;
    transition.target = target;
    const Error announce_error = network->announce_world(target, transition.epoch);
    transition.announced = true;
    if (announce_error != OK) {
        finish_transition(false, "WorldAnnouncementFailed",
                "Could not announce the destination to the session: " + UtilityFunctions::error_string(announce_error) + ".");
        return;
    }
    String diagnostic;
    const Error error = loader->request_coordinated_load(destination->second,
            callable_mp(this, &GameRuntime::publish_world), diagnostic);
    if (error != OK && transition.operation == operation) {
        finish_transition(false, "SceneRejected", diagnostic.is_empty() ?
                "The scene loader rejected the transition: " + UtilityFunctions::error_string(error) + String(".") :
                diagnostic);
    }
}

int64_t GameRuntime::publish_world() {
    if (cleaning || transition.operation.is_null() || !is_authority()) {
        return ERR_UNAUTHORIZED;
    }
    if (transition.save_ticket != 0) {
        const auto operation = transition.operation;
        String failure;
        const bool activated = saves->activate_prepared_load(transition.save_ticket, failure);
        if (cleaning || transition.operation != operation) {
            return ERR_UNAVAILABLE;
        }
        if (!activated) {
            transition.restore_activation_error = failure;
            return ERR_INVALID_DATA;
        }
        transition.restore_warning = failure;
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

void GameRuntime::finish_transition(bool ok, const String &code, const String &message, bool recovery) {
    if (transition.operation.is_null()) {
        return;
    }
    Transition finished = std::exchange(transition, Transition{});
    publishing = false;
    if (!finished.published && finished.save_ticket != 0) {
        saves->cancel(finished.save_ticket);
    }
    const Error confirmation = finished.announced && is_authority() ?
            network->finish_world(finished.epoch, ok, recovery) : OK;
    String result_code = code;
    String result_message = message;
    if (confirmation != OK) {
        if (ok && finished.published) {
            result_code = "WorldConfirmationFailed";
            result_message = "Multiplayer confirmation failed after the world was published: " +
                    UtilityFunctions::error_string(confirmation) + ".";
        } else {
            if (result_message.is_empty()) {
                result_message = "The destination world did not become ready.";
            }
            result_message += " The multiplayer transition outcome could not be sent: " +
                    UtilityFunctions::error_string(confirmation) + ".";
        }
        if (finished.published) {
            ok = false;
            recovery = true;
        }
    }
    if (!recovery && !ok && finished.reopen_lobby && is_authority()) {
        const Error reopen = session_node->set_accepting_connections(true);
        if (reopen != OK) {
            result_message += " The lobby could not reopen connection admission: " +
                    UtilityFunctions::error_string(reopen) + ".";
        }
    }
    const Phase next = recovery ? RECOVERING : ok ? RUNNING : finished.previous;
    if (is_authority()) {
        router->set_accepting_commands(next == RUNNING);
    }
    if (ok) {
        result_message = "World '" + String(finished.target) + "' is ready.";
    } else {
        if (result_message.is_empty()) {
            result_message = finished.save_ticket != 0 ? "The checkpoint could not be restored." :
                                                         "The destination world could not be prepared.";
        }
        if (recovery) {
            result_message += " The session entered recovery and gameplay commands are paused.";
        }
    }
    Dictionary result = outcome(ok, result_code, result_message);
    if (ok && !finished.generation.is_empty()) {
        result["value"] = finished.generation;
    }
    if (ok && !finished.restore_warning.is_empty()) {
        result["warning"] = finished.restore_warning;
    }
    const int64_t ready_epoch = world_epoch;
    set_phase(next);
    if (ok && phase == RUNNING && world_epoch == ready_epoch && !cleaning) {
        emit_signal("world_ready", world_id, world_epoch);
    }
    finished.operation->finish(result);
}

Ref<SaveSlot> GameRuntime::save_slot(const String &profile, const String &slot) {
    Ref<SaveSlot> handle;
    handle.instantiate();
    handle->configure(this, profile, slot);
    return handle;
}

Ref<GameOperation> GameRuntime::save(const String &profile, const String &slot,
        const Dictionary &metadata, bool autosave) {
    const auto operation = GameOperation::for_checkpoint(false, profile, slot);
    String rejection;
    if (immediate_active) {
        rejection = "Use the coordinated game flow to capture a checkpoint.";
    } else if (!is_authority()) {
        rejection = "Capture checkpoints from the game authority.";
    } else if (cleaning || transition.operation.is_valid()) {
        rejection = "A world transition or session cleanup is in progress. Wait for a ready world before saving.";
    } else if (phase != RUNNING) {
        rejection = "Wait for a ready world on the authority before saving.";
    }
    if (!rejection.is_empty()) {
        operation->finish(outcome(false, "SaveNotReady", rejection));
        return operation;
    }
    const int64_t ticket = autosave ? saves->request_autosave(profile, slot, metadata) :
            saves->request_save(profile, slot, metadata);
    if (ticket == 0) {
        const String code = saves->get_last_admission_error();
        operation->finish(outcome(false, code, saves->get_last_admission_message()));
        return operation;
    }
    save_operations.emplace(ticket, operation);
    ++pending_captures;
    return operation;
}

Ref<GameOperation> GameRuntime::restore(const String &profile, const String &slot,
        const String &sequence, const String &generation_id) {
    const auto operation = GameOperation::for_checkpoint(true, profile, slot);
    const String rejection = begin_transition(operation);
    if (!rejection.is_empty()) {
        operation->finish(outcome(false, "RestoreRejected", rejection));
        return operation;
    }
    if (cleaning || transition.operation != operation) {
        return operation;
    }
    transition.save_ticket = saves->request_prepare_load(profile, slot, sequence, generation_id);
    if (transition.save_ticket == 0) {
        const String code = saves->get_last_admission_error();
        finish_transition(false, code, saves->get_last_admission_message());
    }
    return operation;
}

Ref<GameOperation> GameRuntime::save_history(const String &profile, const String &slot,
        const String &before_sequence, const String &before_id, int64_t limit) {
    if (configure_game() != OK) {
        return completed(false, "Unconfigured", "Assign a valid game definition before reading save history.");
    }
    const int64_t ticket = saves->request_list(profile, slot, before_sequence, before_id, limit);
    if (ticket == 0) {
        const String detail = saves->get_last_admission_message();
        return completed(false, saves->get_last_admission_error(),
                detail.is_empty() ? String("The checkpoint coordinator rejected the read.") : detail);
    }
    Ref<GameOperation> operation;
    operation.instantiate();
    save_operations.emplace(ticket, operation);
    return operation;
}

void GameRuntime::capture_saves() {
    const int64_t count = std::exchange(pending_captures, int64_t(0));
    for (int64_t index = 0; index < count; ++index) {
        saves->capture_pending(String::num_int64(transactions->get_revision()));
    }
}

void GameRuntime::on_load_prepared(int64_t ticket, const Dictionary &generation) {
    if (ticket == transition.save_ticket && transition.operation.is_valid() && !cleaning) {
        transition.generation = generation.duplicate(true);
        stage_scene(StringName(checkpoint_adapter->get_candidate_world(saves->get_prepared_candidate(ticket))));
    }
}

void GameRuntime::on_save_finished(int64_t ticket, const Dictionary &result) {
    auto pending = save_operations.extract(ticket);
    if (!pending.empty()) {
        pending.mapped()->finish(result);
    } else if (transition.operation.is_valid() && ticket == transition.save_ticket &&
            !bool(result.get("ok", false)) && !transition.published && !cleaning) {
        finish_transition(false, result.get("code", "RestoreFailed"), result.get("message", ""));
    }
}

void GameRuntime::cancel_save_operations() {
    pending_captures = 0;
    auto pending = std::exchange(save_operations, {});
    for (const auto &[ticket, operation] : pending) {
        const String cancellation = saves->cancel(ticket);
        String message = "The game stopped waiting for this storage operation.";
        if (operation->is_checkpoint_save() && cancellation != "Cancelled") {
            message += " The checkpoint write may have completed. Inspect the slot history before retrying.";
        }
        operation->finish(outcome(false, "SessionEnded", message));
    }
}

void GameRuntime::on_session_started(int64_t role, int64_t local_peer) {
    if (immediate_active) {
        return;
    }
    const Error transport_error = network->bind_transport();
    if (transport_error != OK) {
        UtilityFunctions::printerr("[Crimson] Could not attach match networking to the session (",
                UtilityFunctions::error_string(transport_error), "). Closing the session.");
        session_node->leave();
        return;
    }
    if (role != session::GameSession::CLIENT) {
        match_id = String("match.") + String::num_uint64(Time::get_singleton()->get_ticks_usec());
        const Error router_error = router->configure(match_id, 1, possession);
        if (router_error != OK) {
            UtilityFunctions::printerr("[Crimson] Could not configure the authority command router (",
                    UtilityFunctions::error_string(router_error), "). Closing the session.");
            session_node->leave();
            return;
        }
        for (const auto &[name, command] : commands) {
            if (command.handler.is_valid()) {
                const Error registration = router->register_handler(name, command.handler, command.requires_primary);
                if (registration != OK) {
                    UtilityFunctions::printerr("[Crimson] Could not register command '", name, "' (",
                            UtilityFunctions::error_string(registration), "). Closing the session.");
                    session_node->leave();
                    return;
                }
            }
        }
        if (role == session::GameSession::OFFLINE) {
            if (bindings->bind_peer(local_peer, "local") == 0) {
                UtilityFunctions::printerr("[Crimson] Could not bind the offline player to the local peer. Closing the session.");
                session_node->leave();
                return;
            }
        }
    }
    set_phase(LOBBY);
}

void GameRuntime::on_session_ended(const StringName &reason) {
    if (immediate_active) {
        return;
    }
    cleaning = true;
    network->reset();
    router->set_accepting_commands(false);
    ended_reason = reason;
}

void GameRuntime::cleanup_session() {
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
    report_remote_travel(false, "SessionEnded", "The session ended before world travel completed.");
    publishing = false;
    checkpoint_adapter->discard_publication();
    reset_match();
    const StringName reason = std::exchange(ended_reason, StringName());
    const bool reattached = std::exchange(republish_after_cleanup, false);
    cleaning = false;
    set_phase(IDLE);
    if (reattached) {
        GameServiceContainer::publish(*this);
    } else {
        emit_signal("session_ended", reason);
    }
}

void GameRuntime::on_peer_joined(int64_t peer) {
    if (!immediate_active) {
        emit_signal("peer_joined", peer);
    }
}

void GameRuntime::on_peer_left(int64_t peer) {
    if (immediate_active) {
        return;
    }
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

Error GameRuntime::bind_player(int64_t peer, const String &player_id) {
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

bool GameRuntime::peers_are_bound() const {
    for (const int32_t peer : session_node->get_connected_peer_ids()) {
        if (bindings->get_binding_generation(peer) == 0) {
            return false;
        }
    }
    return true;
}

void GameRuntime::on_world_announced(const StringName &target, int64_t epoch) {
    if (cleaning || session_node->get_state() != session::GameSession::CLIENT ||
            epoch <= world_epoch || remote_epoch != 0 ||
            (phase != LOBBY && phase != RUNNING)) {
        return;
    }
    if (worlds.find(target) == worlds.end()) {
        if (epoch > rejected_remote_epoch) {
            rejected_remote_epoch = epoch;
            const String key(target);
            if (valid_world_key(key)) {
                UtilityFunctions::printerr("[Crimson] World travel (UnknownWorld): The authority announced world '",
                        key, "', which is absent from this client's definition. Check that both builds use the same world catalog.");
            } else {
                UtilityFunctions::printerr("[Crimson] World travel (UnknownWorld): The authority announced an invalid world ID.");
            }
        }
        return;
    }
    remote_epoch = epoch;
    remote_target = target;
    remote_previous = phase;
    remote_outcome_reported = false;
    remote_preparation_error = String();
    set_phase(TRANSITIONING);
}

void GameRuntime::report_remote_travel(bool ok, const char *code, const String &message) {
    if (remote_epoch == 0 || remote_outcome_reported) {
        return;
    }
    remote_outcome_reported = true;
    if (ok) {
        return;
    }
    if (String(code) == "Cancelled" || String(code) == "SessionEnded") {
        UtilityFunctions::print("[Crimson] World travel (world '", remote_target, "', ", code, "): ", message);
    } else {
        UtilityFunctions::printerr("[Crimson] World travel (world '", remote_target, "', ", code, "): ", message);
    }
}

void GameRuntime::on_world_finished(int64_t epoch, bool ok, bool recovery) {
    if (cleaning || session_node->get_state() != session::GameSession::CLIENT) {
        return;
    }
    if (epoch != remote_epoch || remote_epoch == 0) {
        if (!ok && recovery && epoch == world_epoch && phase != RECOVERING) {
            UtilityFunctions::printerr("[Crimson] World synchronization was lost on the authority. The client entered recovery.");
            set_phase(RECOVERING);
        }
        return;
    }
    const StringName target = remote_target;
    Phase next = RECOVERING;
    bool ready = false;
    if (!ok) {
        const String reason = recovery ? "The session entered recovery." : "The previous game phase was restored.";
        const String message = remote_preparation_error.is_empty() ?
                String("The authority could not complete the world transition. ") + reason :
                remote_preparation_error + String(" ") + reason;
        report_remote_travel(false, recovery ? "WorldRecoveryRequired" : "WorldTransitionAborted",
                message);
        next = recovery ? RECOVERING : remote_previous;
    } else if (!replication_node->is_client_ready() || replication_node->get_world_epoch() != epoch ||
            loader->is_loading() || !loader->get_current_scene() || loader->get_current_scene_id() != worlds.at(target)) {
        report_remote_travel(false, "WorldReadinessMismatch",
                "The authority confirmed a world whose local scene or state baseline is unavailable. The session entered recovery.");
    } else {
        world_id = target;
        world_epoch = epoch;
        local_sequence = 0;
        ready = true;
        next = RUNNING;
        report_remote_travel(true, "OK", "The destination scene and its state baseline are ready.");
    }
    remote_target = {};
    remote_epoch = 0;
    remote_preparation_error = String();
    set_phase(next);
    if (ready && phase == RUNNING && !cleaning) {
        emit_signal("projection_updated", replication_node->get_revision(), true);
        emit_signal("world_ready", world_id, world_epoch);
    }
}

void GameRuntime::on_scene_completed(const StringName &) {
    if (immediate_active) {
        return;
    }
    if (!is_authority() || cleaning || transition.operation.is_null()) {
        return;
    }
    if (transition.save_ticket != 0) {
        const Error notification = checkpoint_adapter->notify_publication();
        if (notification != OK) {
            finish_transition(false, "WorldPublicationFailed",
                    "Could not notify checkpoint observers: " + UtilityFunctions::error_string(notification) + String("."), true);
            return;
        }
    }
    const Error command_world = router->advance_world(world_epoch);
    if (command_world != OK) {
        finish_transition(false, "WorldPublicationFailed",
                "Could not advance gameplay commands to the new world: " + UtilityFunctions::error_string(command_world) + String("."), true);
        return;
    }
    publishing = false;
    if (session_node->get_state() == session::GameSession::OFFLINE) {
        finish_transition(true, "OK");
        return;
    }
    const int64_t revision = transactions->get_revision();
    const Error replication_world = replication_node->advance_world(world_epoch, revision);
    if (replication_world != OK) {
        finish_transition(false, "ReplicationFailed",
                "Could not prepare the new replication stream: " + UtilityFunctions::error_string(replication_world) + String("."), true);
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
        const Error baseline = replication_node->send_baseline(peer);
        if (baseline != OK) {
            finish_transition(false, "BaselineFailed",
                    "Could not send a participant's baseline: " + UtilityFunctions::error_string(baseline) + String("."), true);
            return;
        }
    }
}

void GameRuntime::on_scene_failed(const StringName &, int64_t, const String &message) {
    if (cleaning) {
        return;
    }
    if (transition.operation.is_valid()) {
        finish_transition(false, "ScenePreparationFailed", transition.restore_activation_error.is_empty() ? message :
                "Could not activate the prepared checkpoint. " + transition.restore_activation_error);
    } else if (remote_epoch != 0 && !remote_outcome_reported) {
        remote_preparation_error = message;
    }
}

void GameRuntime::on_scene_sync_failed(const StringName &, int64_t, const String &message) {
    if (cleaning || immediate_active) {
        return;
    }
    if (transition.operation.is_valid()) {
        finish_transition(false, "SceneSynchronizationFailed", transition.restore_activation_error.is_empty() ? message :
                "Could not activate the prepared checkpoint. " + transition.restore_activation_error, true);
    } else if (phase == TRANSITIONING) {
        report_remote_travel(false, "SceneSynchronizationFailed",
                message + String(" The session entered recovery and gameplay commands are paused."));
        set_phase(RECOVERING);
    }
}

void GameRuntime::on_peer_ready(int64_t peer, int64_t epoch, int64_t generation) {
    if (!cleaning && transition.operation.is_valid() && transition.epoch == epoch &&
            generation == bindings->get_binding_generation(peer) && transition.waiting.erase(peer) != 0 &&
            transition.waiting.empty()) {
        finish_transition(true, "OK");
    }
}

void GameRuntime::on_peer_sync_failed(int64_t peer, int64_t epoch, int64_t error) {
    if (cleaning || epoch != world_epoch) {
        return;
    }
    if (transition.waiting.count(peer) != 0) {
        finish_transition(false, "BaselineFailed",
                "A required participant could not synchronize state: " + UtilityFunctions::error_string(error) + String("."), true);
    } else if (phase == RUNNING && is_authority()) {
        recover_replication("A required participant lost synchronization with the active world: " +
                UtilityFunctions::error_string(error) + String("."));
    }
}

void GameRuntime::recover_replication(const String &message) {
    const Error command_gate = router->set_accepting_commands(false);
    const Error notification = network->finish_world(world_epoch, false, true);
    UtilityFunctions::printerr("[Crimson] ", message, " The session entered recovery. Gameplay commands are paused.");
    if (command_gate != OK) {
        UtilityFunctions::printerr("[Crimson] The command router rejected the recovery gate (",
                UtilityFunctions::error_string(command_gate), "). The GameRuntime phase still rejects new commands.");
    }
    if (notification != OK) {
        UtilityFunctions::printerr("[Crimson] Could not notify clients about recovery (",
                UtilityFunctions::error_string(notification), ").");
    }
    set_phase(RECOVERING);
}

void GameRuntime::on_projection_updated(int64_t epoch, int64_t revision, bool baseline) {
    if (phase == RUNNING && epoch == world_epoch && !cleaning) {
        emit_signal("projection_updated", revision, baseline);
    }
}

Error GameRuntime::declare_command(const String &name, bool requires_primary) {
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

Error GameRuntime::register_command(const String &name, const Callable &handler, bool requires_primary) {
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
Error GameRuntime::set_shared_stores(const PackedStringArray &store_ids) {
    if (phase != IDLE && phase != LOBBY) {
        return ERR_BUSY;
    }
    for (const String &store : store_ids) {
        if (store.is_empty() || store == context_record || store == "crimson.inventory") {
            return ERR_INVALID_PARAMETER;
        }
    }
    shared_stores = store_ids;
    return OK;
}

Error GameRuntime::set_projector(const Callable &projector) {
    if (phase != IDLE && phase != LOBBY) {
        return ERR_BUSY;
    }
    project_view = projector;
    return OK;
}

Dictionary GameRuntime::project(int64_t peer, const String &player, int64_t epoch, int64_t revision) {
    Dictionary view;
    if (project_view.is_valid()) {
        const Variant projected = project_view.call(peer, player, epoch, revision);
        if (projected.get_type() != Variant::DICTIONARY) {
            return outcome(false, "InvalidProjection", "The game projector must return a Dictionary.");
        }
        const Dictionary reply = projected;
        const Variant records = reply.get("view", Variant());
        if (!bool(reply.get("ok", false))) {
            const Variant detail = reply.get("message", "");
            const String message = detail.get_type() == Variant::STRING ? String(detail) : String();
            return outcome(false, "InvalidProjection", message.strip_edges().is_empty() ?
                    String("The game projector rejected the recipient's committed view.") : message);
        }
        if (records.get_type() != Variant::DICTIONARY) {
            return outcome(false, "InvalidProjection", "The game projector must return the view as a Dictionary.");
        }
        view = Dictionary(records).duplicate();
    } else {
        for (const String &store : shared_stores) {
            if (transactions->get_store_revision(store) < 0) {
                return outcome(false, "UnknownStore", "The projection references unregistered store '" + store + String("'."));
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

Dictionary GameRuntime::dispatch_remote(int64_t peer, const Dictionary &intent) {
    const int64_t generation = bindings->get_binding_generation(peer);
    if (!is_authority() || phase != RUNNING || cleaning || generation == 0 ||
            !replication_node->is_peer_ready(peer, generation)) {
        return outcome(false, "PeerNotReady", "The peer has no ready and admitted player binding.");
    }
    return router->submit(bindings->get_player(peer), intent);
}

Dictionary GameRuntime::local_command_context() const {
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

Ref<GameOperation> GameRuntime::submit(const String &name, const Dictionary &payload) {
    if (immediate_active || phase != RUNNING || cleaning || local_sequence == std::numeric_limits<int64_t>::max()) {
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
    Ref<GameOperation> operation;
    operation.instantiate();
    operation->finish(router->submit(player, intent));
    return operation;
}

GameRuntime::Phase GameRuntime::get_phase() const {
    return phase;
}

StringName GameRuntime::get_world_id() const {
    return world_id;
}

int64_t GameRuntime::get_world_epoch() const {
    return world_epoch;
}

Node *GameRuntime::get_world() const {
    if (immediate_active) {
        return immediate ? immediate->get_world() : nullptr;
    }
    return loader ? loader->get_current_scene() : nullptr;
}

Node3D *GameRuntime::get_local_player() const {
    return immediate_active && immediate ? immediate->get_local_player() : nullptr;
}

Node3D *GameRuntime::get_player(int64_t peer_id) const {
    return immediate_active && immediate ? immediate->get_player(peer_id) : nullptr;
}

Ref<match_runtime::TransactionCoordinator> GameRuntime::get_transactions() const {
    if (immediate_active) {
        return {};
    }
    return session_node && (session_node->get_state() == session::GameSession::CLIENT ||
            session_node->get_state() == session::GameSession::CONNECTING) ?
            Ref<match_runtime::TransactionCoordinator>() : transactions;
}

Ref<match_runtime::PlayerPossession> GameRuntime::get_possession() const {
    return get_transactions().is_valid() ? possession : Ref<match_runtime::PlayerPossession>();
}

String GameRuntime::get_local_player_id() const {
    if (!session_node || immediate_active || phase == IDLE || cleaning) {
        return {};
    }
    return local_command_context().get("player_id", String());
}

String GameRuntime::get_local_character_id() const {
    if (immediate_active || phase != RUNNING || cleaning) {
        return {};
    }
    return local_command_context().get("character_id", String());
}

Dictionary GameRuntime::get_projection() const {
    Dictionary view = !immediate_active && phase == RUNNING && replication_node ? replication_node->get_projection() : Dictionary();
    view.erase(context_record);
    return view;
}

bool GameRuntime::is_authority() const {
    if (immediate_active) {
        return immediate && immediate->is_authority();
    }
    return session_node && session_node->is_authority();
}

} // namespace crimson::integration
