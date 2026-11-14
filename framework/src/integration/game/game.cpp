// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#include "integration/game/game.hpp"

#include "integration/adapters/game_inventory.hpp"
#include "integration/game/crimson_game.hpp"
#include "integration/game/game_network_diagnostics.hpp"
#include "integration/online/steam/steam_code_first.hpp"
#include "integration/online/steam/steam_online.hpp"
#include "integration/player_control/player_control.hpp"
#include "modules/mover/mover_component.hpp"
#include "modules/network/network_codec.hpp"
#include "modules/steam/client/steam_config.hpp"
#include "modules/steam/client/steamworks.hpp"
#include "modules/steam/server/steam_server.hpp"

#include <godot_cpp/classes/character_body3d.hpp>
#include <godot_cpp/classes/os.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/core/memory.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include <utility>

using namespace godot;

namespace crimson::integration {
namespace {

// Scope selection and script factories run on the Godot scene thread.
bool on_scene_thread() {
    const auto *os = OS::get_singleton();
    return os && os->get_thread_caller_id() == os->get_main_thread_id();
}

// Snapshot a definition without allowing a branch to mutate its source.
Ref<GameDefinition> copy_definition(const Ref<GameDefinition> &definition) {
    return definition.is_valid() ? definition->with_provider(definition->get_service_provider()) :
            GameDefinition::configure();
}

// Report a rejected boot at its owning boundary and retain the same diagnostic for actions.
Ref<GameResult> boot_failure(const String &message) {
    UtilityFunctions::printerr("[Crimson] Game could not boot. ", message);
    return GameResult::make(false, "GameBootFailed", message);
}

} // namespace

void WorldDefinition::_bind_methods() {
    ClassDB::bind_static_method(get_class_static(), D_METHOD("at", "id", "path"), &WorldDefinition::at);
    ClassDB::bind_method(D_METHOD("set_id", "id"), &WorldDefinition::set_id);
    ClassDB::bind_method(D_METHOD("get_id"), &WorldDefinition::get_id);
    ClassDB::bind_method(D_METHOD("set_path", "path"), &WorldDefinition::set_path);
    ClassDB::bind_method(D_METHOD("get_path"), &WorldDefinition::get_path);
    ADD_PROPERTY(PropertyInfo(Variant::STRING_NAME, "id"), "set_id", "get_id");
    ADD_PROPERTY(PropertyInfo(Variant::STRING, "path", PROPERTY_HINT_FILE, "*.tscn,*.scn"), "set_path", "get_path");
}

Ref<WorldDefinition> WorldDefinition::at(const StringName &id, const String &path) {
    Ref<WorldDefinition> world;
    world.instantiate();
    world->set_id(id);
    world->set_path(path);
    return world;
}

void WorldDefinition::set_id(const StringName &value) { id = value; }
StringName WorldDefinition::get_id() const { return id; }
void WorldDefinition::set_path(const String &value) { path = value; }
String WorldDefinition::get_path() const { return path; }

void GameBindings::_bind_methods() {
    ClassDB::bind_method(D_METHOD("session_transport", "factory"), &GameBindings::session_transport);
}

Error GameBindings::session_transport(const Callable &factory) {
    if (sealed) {
        return ERR_BUSY;
    }
    if (!factory.is_valid()) {
        invalid = true;
        return ERR_INVALID_PARAMETER;
    }
    transport_factory = factory;
    return OK;
}

void GameProvider::_bind_methods() {
    GDVIRTUAL_BIND(_register, "services");
}

GameConfig::GameConfig() = default;
GameConfig::~GameConfig() = default;

void GameConfig::_bind_methods() {
    ClassDB::bind_static_method(get_class_static(), D_METHOD("from_definition", "definition"),
            &GameConfig::from_definition);
    ClassDB::bind_method(D_METHOD("lan", "players"), &GameConfig::lan, DEFVAL(2));
    ClassDB::bind_method(D_METHOD("steam", "settings", "players"), &GameConfig::steam,
            DEFVAL(Ref<steam::SteamConfig>()), DEFVAL(2));
    ClassDB::bind_method(D_METHOD("steam_server", "settings", "players"), &GameConfig::steam_server, DEFVAL(2));
    ClassDB::bind_method(D_METHOD("on_port", "port"), &GameConfig::on_port);
    ClassDB::bind_method(D_METHOD("world_id", "id"), &GameConfig::world_id);
    ClassDB::bind_method(D_METHOD("with_world", "world"), &GameConfig::with_world);
    ClassDB::bind_method(D_METHOD("provider", "provider"), &GameConfig::provider);
    ClassDB::bind_method(D_METHOD("inventory", "settings"), &GameConfig::inventory);
    ClassDB::bind_method(D_METHOD("player_controls", "profile"), &GameConfig::player_controls, DEFVAL(Ref<PlayerControlProfile>()));
    ClassDB::bind_method(D_METHOD("boot", "owner"), &GameConfig::boot);
    ClassDB::bind_method(D_METHOD("boot_server", "owner"), &GameConfig::boot_server);
}

Ref<GameConfig> GameConfig::from_definition(const Ref<GameDefinition> &source) {
    Ref<GameConfig> draft;
    draft.instantiate();
    draft->definition = copy_definition(source);
    if (source.is_valid()) {
        draft->coordinated = source->get_starting_world_scene().is_null() && !source->get_worlds().is_empty();
        draft->online = Object::cast_to<SteamProvider>(source->get_service_provider().ptr()) != nullptr;
    }
    return draft;
}

Ref<GameConfig> GameConfig::branch() const {
    auto draft = from_definition(definition);
    draft->overrides = overrides;
    draft->inventory_config = inventory_config;
    draft->control_profile = control_profile;
    draft->use_player_controls = use_player_controls;
    draft->coordinated = coordinated;
    draft->online = online;
    draft->invalid = invalid;
    return draft;
}

Ref<GameConfig> GameConfig::lan(int64_t players) const {
    auto draft = branch();
    if (players < 1 || players > WorldSession::MAX_PLAYERS) {
        draft->invalid = "Choose a player count from 1 to 64.";
        return draft;
    }
    const auto rules = draft->definition->get_multiplayer_rules();
    draft->definition = draft->definition->with_provider({})->with_multiplayer(
            (rules.is_valid() ? rules : MultiplayerRules::listen_coop())->with_max_players(players));
    draft->online = false;
    return draft;
}

Ref<GameConfig> GameConfig::steam(const Ref<steam::SteamConfig> &settings, int64_t players) const {
    auto draft = lan(players);
    const auto provider = make_steam_provider(settings);
    if (provider.is_null()) {
        draft->invalid = "Choose valid Steam settings and a matching test group for Spacewar.";
    } else {
        draft->definition = draft->definition->with_provider(provider);
        draft->online = true;
    }
    return draft;
}

Ref<GameConfig> GameConfig::steam_server(const Ref<steam::SteamServer> &settings, int64_t players) const {
    auto draft = lan(players);
    if (settings.is_null() || !settings->is_valid()) {
        draft->invalid = "Choose valid Steam dedicated server settings.";
    } else {
        draft->definition = draft->definition->with_provider(SteamProvider::configure()->dedicated(settings));
        draft->online = true;
    }
    return draft;
}

Ref<GameConfig> GameConfig::on_port(int64_t port) const {
    auto draft = branch();
    const auto rules = draft->definition->get_multiplayer_rules();
    if (rules.is_null() || port < 1 || port > 65535) {
        draft->invalid = "Select multiplayer and a UDP port from 1 to 65535.";
    } else {
        draft->definition = draft->definition->with_multiplayer(rules->on_port(port));
    }
    return draft;
}

Ref<GameConfig> GameConfig::world_id(const StringName &id) const {
    auto draft = branch();
    draft->definition = draft->definition->starting_in(id);
    return draft;
}

bool GameConfig::put_world(Dictionary &worlds, const Ref<WorldDefinition> &world) {
    if (world.is_null() || world->get_id().is_empty() || world->get_path().is_empty()) {
        return false;
    }
    Dictionary entry;
    entry["scene_id"] = String(world->get_id());
    entry["path"] = world->get_path();
    const String id = world->get_id();
    if (worlds.has(id) && worlds[id] != Variant(entry)) {
        return false;
    }
    worlds[id] = entry;
    return true;
}

Ref<GameConfig> GameConfig::with_world(const Ref<WorldDefinition> &world) const {
    auto draft = branch();
    Dictionary worlds = draft->definition->get_worlds();
    if (!coordinated || !put_world(worlds, world)) {
        draft->invalid = "Add a world with a durable ID and scene path to a coordinated game.";
    } else {
        draft->definition = draft->definition->with_worlds(worlds);
    }
    return draft;
}

Ref<GameConfig> GameConfig::provider(const Ref<GameProvider> &provider) const {
    auto draft = branch();
    draft->overrides = provider;
    return draft;
}

Ref<GameConfig> GameConfig::inventory(const Ref<InventoryConfig> &settings) const {
    auto draft = branch();
    draft->inventory_config = settings;
    return draft;
}

Ref<GameConfig> GameConfig::player_controls(const Ref<PlayerControlProfile> &profile) const {
    auto draft = branch();
    draft->control_profile = profile;
    draft->use_player_controls = true;
    return draft;
}

Ref<Game> GameConfig::boot(Node *owner) const {
    Ref<Game> game;
    game.instantiate();
    if (!on_scene_thread() || !owner || !owner->is_inside_tree() || owner->is_queued_for_deletion()) {
        game->boot_outcome = boot_failure("Pass a live scene owner on the main thread.");
        return game;
    }
    if (!invalid.is_empty()) {
        game->boot_outcome = boot_failure(invalid);
        return game;
    }
    if (definition.is_null() || definition->get_starting_world().is_empty() ||
            (!coordinated && (definition->get_starting_world_scene().is_null() || definition->get_player_scene().is_null()))) {
        game->boot_outcome = boot_failure("Choose a starting world and, for fixed-world play, a player scene.");
        return game;
    }
    if (!coordinated && inventory_config.is_valid()) {
        game->boot_outcome = boot_failure("Use Game.campaign() for game-bound inventory.");
        return game;
    }
    if (use_player_controls && (coordinated || (control_profile.is_valid() && !control_profile->has_valid_motion_settings()))) {
        game->boot_outcome = boot_failure("Choose valid player motion settings on a fixed-world game.");
        return game;
    }
    const ObjectID owner_id(owner->get_instance_id());
    WorldSession::MovementSettings movement;
    if (use_player_controls) {
        Ref<mover::MotionRule> authored;
        if (control_profile.is_valid()) {
            movement.speed = control_profile->get_move_speed();
            movement.reference = control_profile->get_move_relative_to();
            movement.style = control_profile->get_controller_style();
            movement.predict = control_profile->is_predict_movement_enabled();
            authored = control_profile->get_movement_rule();
        } else {
            Node *actor = definition->get_player_scene()->instantiate();
            const auto *body = Object::cast_to<CharacterBody3D>(actor);
            bool duplicate = false;
            const auto *component = body ? mover::MoverComponent::find_on(actor, duplicate) : nullptr;
            const bool valid = component && !duplicate && component->has_valid_settings() &&
                    body->get_motion_mode() == CharacterBody3D::MOTION_MODE_GROUNDED;
            if (valid) {
                authored = component->snapshot_rule();
                movement.speed = component->get_move_speed();
                movement.reference = component->get_move_relative_to();
                movement.style = component->get_controller_style();
                movement.predict = component->is_predict_movement_enabled();
            }
            if (actor) {
                memdelete(actor);
            }
            if (!valid) {
                game->boot_outcome = boot_failure("Add one valid MoverComponent directly under the player scene's CharacterBody3D root.");
                return game;
            }
            owner = Object::cast_to<Node>(ObjectDB::get_instance(owner_id));
            if (!owner || !owner->is_inside_tree() || owner->is_queued_for_deletion()) {
                game->boot_outcome = boot_failure("The game owner changed while reading the player scene.");
                return game;
            }
        }
        movement.rule = control_profile.is_valid() ? mover::MotionRule::snapshot(authored) : authored;
    }
    if (auto *occupied = owner->get_node_or_null(NodePath("Game"))) {
        auto *previous = Object::cast_to<GameRuntime>(occupied);
        if (!previous || !previous->code_first_retired) {
            game->boot_outcome = boot_failure("This owner already has a child named Game.");
            return game;
        }
        const ObjectID previous_id(previous->get_instance_id());
        owner->remove_child(previous);
        if (auto *retired = Object::cast_to<Node>(ObjectDB::get_instance(previous_id))) {
            retired->queue_free();
        }
    }
    Ref<session::SessionTransport> transport;
    if (overrides.is_valid()) {
        Ref<GameBindings> bindings;
        bindings.instantiate();
        GDVIRTUAL_CALL_PTR(overrides.ptr(), _register, bindings);
        bindings->sealed = true;
        if (bindings->transport_factory.is_valid()) {
            if (online || definition->get_service_provider().is_valid()) {
                game->boot_outcome = boot_failure("Choose the transport override or the provider that owns the connection.");
                return game;
            }
            transport = bindings->transport_factory.call();
            if (transport.is_null()) {
                bindings->invalid = true;
            }
        }
        if (bindings->invalid) {
            game->boot_outcome = boot_failure("The session transport factory must return a SessionTransport.");
            return game;
        }
    }
    owner = Object::cast_to<Node>(ObjectDB::get_instance(owner_id));
    if (!owner || !owner->is_inside_tree() || owner->is_queued_for_deletion() || owner->has_node(NodePath("Game"))) {
        game->boot_outcome = boot_failure("The game owner changed during provider setup.");
        return game;
    }

    auto *runtime = definition->create();
    runtime->code_first_transport = transport;
    runtime->player_movement = movement;
    runtime->code_first_owned = true;
    const ObjectID runtime_id(runtime->get_instance_id());
    owner->add_child(runtime, false, Node::INTERNAL_MODE_BACK);
    runtime = Object::cast_to<GameRuntime>(ObjectDB::get_instance(runtime_id));
    const auto attachment = GameServiceContainer::scenes_for(runtime);
    const auto *parent = runtime ? runtime->get_parent() : nullptr;
    if (attachment.is_null() || !parent || parent->is_queued_for_deletion() ||
            parent->get_instance_id() != uint64_t(owner_id) || runtime->get_name() != StringName("Game")) {
        const String detail = runtime && !runtime->service_setup_error.is_empty() ? runtime->service_setup_error :
                String("The game services could not attach to their owner.");
        if (runtime && runtime->get_parent()) {
            runtime->queue_free();
        } else if (runtime) {
            memdelete(runtime);
        }
        game->boot_outcome = boot_failure(detail);
        return game;
    }

    game->game_id = runtime_id;
    game->attachment = attachment;
    game->coordinated = coordinated;
    game->online = online || GameServiceContainer::online_for(runtime).is_valid();
    if (coordinated && runtime->configure_game() != OK) {
        runtime->queue_free();
        game->boot_outcome = boot_failure("Choose a valid starting world, scene catalog, and checkpoint configuration.");
        return game;
    }
    game->scene_actions.instantiate();
    game->scene_actions->service = attachment;
    if (inventory_config.is_valid()) {
        game->inventory_service = GameInventory::attach(runtime, inventory_config);
        if (game->inventory_service.is_null()) {
            runtime->queue_free();
            game->boot_outcome = boot_failure("The configured inventory service could not attach.");
            return game;
        }
    }
    if (!GameServiceContainer::bind_facade(*runtime, game)) {
        runtime->queue_free();
        game->boot_outcome = boot_failure("The game scope retired before it could publish.");
        return game;
    }

    game->boot_outcome = GameResult::make(true, "Ok", "Game is configured.");
    game->codec_service.instantiate();
    game->steam_service = steamworks_for_game(runtime);
    runtime->connect("phase_changed", callable_mp(game.ptr(), &Game::on_phase_changed));
    runtime->connect("peer_joined", callable_mp(game.ptr(), &Game::on_peer_joined));
    runtime->connect("session_ended", callable_mp(game.ptr(), &Game::on_session_ended));
    runtime->connect("world_ready", callable_mp(game.ptr(), &Game::on_world_ready));
    game->observe_invites(*runtime);
    return game;
}

void GameScenes::_bind_methods() {
    ClassDB::bind_method(D_METHOD("load", "world"), &GameScenes::load);
    ClassDB::bind_method(D_METHOD("get_world"), &GameScenes::get_world);
}

Variant GameScenes::load(const Ref<WorldDefinition> &world) {
    if (service.is_null() || world.is_null() || world->get_id().is_empty()) {
        return GameResult::make(false, "InvalidWorld", "Choose a registered world on a live game.");
    }
    return service->load(world->get_id())->wait_result();
}

Node *GameScenes::get_world() const {
    return service.is_valid() ? service->get_world() : nullptr;
}

Game::Game() = default;
Game::~Game() = default;

void Game::_bind_methods() {
    ClassDB::bind_static_method(get_class_static(), D_METHOD("configure", "level", "player"), &Game::configure);
    ClassDB::bind_static_method(get_class_static(), D_METHOD("campaign", "start"), &Game::campaign);
    ClassDB::bind_static_method(get_class_static(), D_METHOD("current"), &Game::current);
    ClassDB::bind_static_method(get_class_static(), D_METHOD("for_node", "node"), &Game::for_node);
    ClassDB::bind_static_method(get_class_static(), D_METHOD("set_default", "game"), &Game::set_default);
    ClassDB::bind_method(D_METHOD("boot_result"), &Game::boot_result);
    ClassDB::bind_method(D_METHOD("play"), &Game::play);
    ClassDB::bind_method(D_METHOD("host", "bind_address", "public_address"), &Game::host,
            DEFVAL(String("*")), DEFVAL(String()));
    ClassDB::bind_method(D_METHOD("join", "address"), &Game::join);
    ClassDB::bind_method(D_METHOD("serve", "bind_address", "public_address"), &Game::serve,
            DEFVAL(String("*")), DEFVAL(String()));
    ClassDB::bind_method(D_METHOD("find_and_join"), &Game::find_and_join);
    ClassDB::bind_method(D_METHOD("join_listing", "listing"), &Game::join_listing);
    ClassDB::bind_method(D_METHOD("open_lobby", "bind_address"), &Game::open_lobby, DEFVAL(String("*")));
    ClassDB::bind_method(D_METHOD("join_lobby", "address"), &Game::join_lobby);
    ClassDB::bind_method(D_METHOD("bind_player", "peer_id", "player_id"), &Game::bind_player);
    ClassDB::bind_method(D_METHOD("start_match", "world"), &Game::start_match, DEFVAL(StringName()));
    ClassDB::bind_method(D_METHOD("invite_friends"), &Game::invite_friends);
    ClassDB::bind_method(D_METHOD("accept_invite", "invite"), &Game::accept_invite);
    ClassDB::bind_method(D_METHOD("pending_invite"), &Game::pending_invite);
    ClassDB::bind_method(D_METHOD("dismiss_invite", "invite"), &Game::dismiss_invite);
    ClassDB::bind_method(D_METHOD("leave"), &Game::leave);
    ClassDB::bind_method(D_METHOD("perform", "name", "payload"), &Game::perform, DEFVAL(Dictionary()));
    ClassDB::bind_method(D_METHOD("register_command", "name", "handler", "requires_primary"),
            &Game::register_command, DEFVAL(false));
    ClassDB::bind_method(D_METHOD("transactions"), &Game::transactions);
    ClassDB::bind_method(D_METHOD("save_slot", "profile", "slot"), &Game::save_slot);
    ClassDB::bind_method(D_METHOD("scenes"), &Game::scenes);
    ClassDB::bind_method(D_METHOD("inventory"), &Game::inventory);
    ClassDB::bind_method(D_METHOD("codecs"), &Game::codecs);
    ClassDB::bind_method(D_METHOD("steam"), &Game::steam);
    ClassDB::bind_method(D_METHOD("get_phase"), &Game::get_phase);
    ClassDB::bind_method(D_METHOD("network_diagnostics"), &Game::network_diagnostics);
    ClassDB::bind_method(D_METHOD("is_player_controlled"), &Game::is_player_controlled);
    ClassDB::bind_method(D_METHOD("get_port"), &Game::get_port);
    ClassDB::bind_method(D_METHOD("get_world"), &Game::get_world);
    ClassDB::bind_method(D_METHOD("get_local_player"), &Game::get_local_player);
    ClassDB::bind_method(D_METHOD("get_player", "peer_id"), &Game::get_player);
    ADD_SIGNAL(MethodInfo("phase_changed", PropertyInfo(Variant::INT, "phase")));
    ADD_SIGNAL(MethodInfo("peer_joined", PropertyInfo(Variant::INT, "peer_id")));
    ADD_SIGNAL(MethodInfo("session_ended", PropertyInfo(Variant::STRING_NAME, "reason")));
    ADD_SIGNAL(MethodInfo("world_ready", PropertyInfo(Variant::STRING_NAME, "world"), PropertyInfo(Variant::INT, "epoch")));
    ADD_SIGNAL(MethodInfo("invite_changed", PropertyInfo(Variant::OBJECT, "invite", PROPERTY_HINT_NONE, "",
            PROPERTY_USAGE_DEFAULT, "SteamInvite")));
    BIND_ENUM_CONSTANT(IDLE);
    BIND_ENUM_CONSTANT(LOBBY);
    BIND_ENUM_CONSTANT(TRANSITIONING);
    BIND_ENUM_CONSTANT(RUNNING);
    BIND_ENUM_CONSTANT(RECOVERING);
}

Ref<GameConfig> Game::configure(const Ref<PackedScene> &level, const Ref<PackedScene> &player) {
    return GameConfig::from_definition(GameDefinition::configure()->with_starting_world("start", level)->with_player(player));
}

Ref<GameConfig> Game::campaign(const Ref<WorldDefinition> &start) {
    auto draft = GameConfig::from_definition({});
    draft->coordinated = true;
    Dictionary worlds;
    if (!GameConfig::put_world(worlds, start)) {
        draft->invalid = "Choose a starting world with a durable ID and scene path.";
    } else {
        draft->definition = draft->definition->with_worlds(worlds)->starting_in(start->get_id());
    }
    return draft;
}

Ref<Game> Game::current() {
    return GameServiceContainer::facade_for(GameServiceContainer::default_game());
}

Ref<Game> Game::for_node(Node *node) {
    if (!on_scene_thread()) {
        return {};
    }
    for (auto *ancestor = node; ancestor; ancestor = ancestor->get_parent()) {
        auto *runtime = Object::cast_to<GameRuntime>(ancestor);
        if (!runtime) {
            runtime = Object::cast_to<GameRuntime>(ancestor->get_node_or_null(NodePath("Game")));
        }
        if (GameServiceContainer::scenes_for(runtime).is_valid()) {
            return GameServiceContainer::facade_for(runtime);
        }
    }
    return {};
}

Error Game::set_default(const Ref<Game> &game) {
    if (game.is_null()) {
        return GameServiceContainer::select_default(nullptr);
    }
    GameRuntime *runtime = game->resolve_game();
    return runtime ? GameServiceContainer::select_default(runtime) : ERR_UNAVAILABLE;
}

GameRuntime *Game::resolve_game() const {
    if (!on_scene_thread() || attachment.is_null() || boot_outcome.is_null() || !boot_outcome->is_ok()) {
        return nullptr;
    }
    auto *runtime = Object::cast_to<GameRuntime>(ObjectDB::get_instance(game_id));
    return GameServiceContainer::scenes_for(runtime) == attachment ? runtime : nullptr;
}

Variant Game::await_operation(const Ref<GameOperation> &operation) {
    return operation.is_valid() ? operation->wait_result() :
            Variant(GameResult::make(false, "Unavailable", "The game could not start that action."));
}

Ref<GameResult> Game::reject(const String &code, const String &message) const {
    return boot_outcome.is_valid() && !boot_outcome->is_ok() ? boot_outcome : GameResult::make(false, code, message);
}

Ref<GameResult> Game::boot_result() const {
    return boot_outcome.is_valid() ? boot_outcome : reject("Unavailable", "Boot the game from a live scene owner.");
}

bool Game::uses_online() const { return online; }

Error Game::prepare_authority() {
    return inventory_service.is_valid() ? inventory_service->prepare() : OK;
}

Variant Game::play() {
    GameRuntime *runtime = resolve_game();
    if (!runtime) {
        return reject("Unavailable", "The game attachment is unavailable.");
    }
    if (prepare_authority() != OK) {
        return reject("InventoryUnavailable", "The game's inventory could not prepare its authority store.");
    }
    runtime = resolve_game();
    if (!runtime) {
        return reject("Unavailable", "The game attachment retired during inventory setup.");
    }
    return await_operation(coordinated ? runtime->start_offline() : runtime->play_solo());
}

Variant Game::leave() {
    GameRuntime *runtime = resolve_game();
    if (!runtime) {
        return reject("Unavailable", "The game attachment is unavailable.");
    }
    if (leave_operation.is_valid()) {
        return await_operation(leave_operation);
    }
    Ref<GameOperation> operation;
    operation.instantiate();
    leave_operation = operation;
    const bool waits_for_session = runtime->immediate_active ||
            (runtime->session_node && runtime->session_node->get_state() != session::GameSession::IDLE);
    const Ref<Game> keep_alive(this);
    const Error error = runtime->leave();
    if (leave_operation == operation && (error != OK || !waits_for_session)) {
        leave_operation.unref();
        Dictionary outcome;
        outcome["ok"] = error == OK;
        outcome["code"] = error == OK ? "Ok" : "GameBusy";
        outcome["message"] = error == OK ? String("Game left.") : String("The game cannot leave during publication. Try again.");
        operation->finish(outcome);
    }
    return await_operation(operation);
}

void Game::on_phase_changed(int64_t phase) {
    const Ref<Game> keep_alive(this);
    emit_signal("phase_changed", phase);
}

void Game::on_session_ended(const StringName &reason) {
    const Ref<Game> keep_alive(this);
    auto operation = std::exchange(leave_operation, {});
    emit_signal("session_ended", reason);
    if (operation.is_valid()) {
        Dictionary outcome;
        outcome["ok"] = true;
        outcome["code"] = "Ok";
        outcome["message"] = "Game left.";
        outcome["reason"] = reason;
        operation->finish(outcome);
    }
}

void Game::on_world_ready(const StringName &world, int64_t epoch) {
    const Ref<Game> keep_alive(this);
    emit_signal("world_ready", world, epoch);
}

void Game::on_scope_closed() {
    if (codec_service.is_valid()) {
        codec_service->close();
    }
    auto operation = std::exchange(leave_operation, {});
    if (operation.is_valid()) {
        Dictionary outcome;
        outcome["ok"] = false;
        outcome["code"] = "Cancelled";
        outcome["message"] = "The game owner left the scene tree.";
        operation->finish(outcome);
    }
}

Variant Game::perform(const String &name, const Dictionary &payload) {
    auto *runtime = resolve_game();
    return runtime ? await_operation(runtime->submit(name, payload)) :
            Variant(reject("Unavailable", "The game attachment is unavailable."));
}

Error Game::register_command(const String &name, const Callable &handler, bool requires_primary) {
    auto *runtime = resolve_game();
    return runtime ? runtime->register_command(name, handler, requires_primary) : ERR_UNAVAILABLE;
}

Ref<match_runtime::TransactionCoordinator> Game::transactions() const {
    const auto *runtime = resolve_game();
    return runtime ? runtime->get_transactions() : Ref<match_runtime::TransactionCoordinator>();
}

Ref<SaveSlot> Game::save_slot(const String &profile, const String &slot) {
    auto *runtime = resolve_game();
    return runtime ? runtime->save_slot(profile, slot) : Ref<SaveSlot>();
}

Ref<GameScenes> Game::scenes() const { return scene_actions; }
Ref<GameInventory> Game::inventory() const { return resolve_game() ? inventory_service : Ref<GameInventory>(); }
Ref<replication::NetworkCodecs> Game::codecs() const {
    return resolve_game() ? codec_service : Ref<replication::NetworkCodecs>();
}

Ref<steam::Steamworks> Game::steam() const {
    return steam_service.is_valid() ? steam_service : steamworks_for_game(nullptr);
}

Game::Phase Game::get_phase() const {
    const auto *runtime = resolve_game();
    return runtime ? static_cast<Phase>(runtime->get_phase()) : IDLE;
}

bool Game::is_player_controlled() const {
    const auto *runtime = resolve_game();
    return runtime && runtime->player_movement.rule.is_valid();
}

int64_t Game::get_port() const {
    const auto *runtime = resolve_game();
    const auto definition = runtime ? runtime->get_definition() : Ref<GameDefinition>();
    const auto rules = definition.is_valid() ? definition->get_multiplayer_rules() : Ref<MultiplayerRules>();
    return rules.is_valid() ? rules->get_port() : 0;
}

Node *Game::get_world() const {
    const auto *runtime = resolve_game();
    return runtime ? runtime->get_world() : nullptr;
}

Node3D *Game::get_local_player() const {
    const auto *runtime = resolve_game();
    return runtime ? runtime->get_local_player() : nullptr;
}

Node3D *Game::get_player(int64_t peer_id) const {
    const auto *runtime = resolve_game();
    return runtime ? runtime->get_player(peer_id) : nullptr;
}

} // namespace crimson::integration
