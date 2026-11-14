// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#include "integration/game/dedicated_boot.hpp"

#include "integration/game/crimson_game.hpp"
#include "integration/game/game.hpp"

#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/os.hpp>
#include <godot_cpp/classes/scene_multiplayer.hpp>
#include <godot_cpp/classes/scene_tree.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/callable_method_pointer.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

namespace crimson::integration {

using namespace godot;

void DedicatedServerEntry::_bind_methods() {
    ClassDB::bind_method(D_METHOD("set_level_scene", "scene"), &DedicatedServerEntry::set_level_scene);
    ClassDB::bind_method(D_METHOD("get_level_scene"), &DedicatedServerEntry::get_level_scene);
    ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "level_scene", PROPERTY_HINT_RESOURCE_TYPE, "PackedScene"),
            "set_level_scene", "get_level_scene");
    ClassDB::bind_method(D_METHOD("set_player_scene", "scene"), &DedicatedServerEntry::set_player_scene);
    ClassDB::bind_method(D_METHOD("get_player_scene"), &DedicatedServerEntry::get_player_scene);
    ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "player_scene", PROPERTY_HINT_RESOURCE_TYPE, "PackedScene"),
            "set_player_scene", "get_player_scene");
    ClassDB::bind_method(D_METHOD("set_world_id", "id"), &DedicatedServerEntry::set_world_id);
    ClassDB::bind_method(D_METHOD("get_world_id"), &DedicatedServerEntry::get_world_id);
    ADD_PROPERTY(PropertyInfo(Variant::STRING_NAME, "world_id"), "set_world_id", "get_world_id");
    ClassDB::bind_method(D_METHOD("set_rules", "rules"), &DedicatedServerEntry::set_rules);
    ClassDB::bind_method(D_METHOD("get_rules"), &DedicatedServerEntry::get_rules);
    ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "rules", PROPERTY_HINT_RESOURCE_TYPE, "MultiplayerRules"),
            "set_rules", "get_rules");
    ClassDB::bind_method(D_METHOD("set_relay_between_guests", "enabled"), &DedicatedServerEntry::set_relay_between_guests);
    ClassDB::bind_method(D_METHOD("get_relay_between_guests"), &DedicatedServerEntry::get_relay_between_guests);
    ADD_PROPERTY(PropertyInfo(Variant::BOOL, "relay_between_guests"), "set_relay_between_guests", "get_relay_between_guests");
}

void DedicatedServerEntry::_ready() {
    if (Engine::get_singleton()->is_editor_hint() ||
            !OS::get_singleton()->get_cmdline_user_args().has("--crimson-role=server")) {
        return;
    }
    if (!relay_between_guests) {
        Ref<SceneMultiplayer> multiplayer = get_multiplayer();
        if (multiplayer.is_valid()) {
            multiplayer->set_server_relay_enabled(false);
        }
    }
    const Ref<GameDefinition> definition = GameDefinition::configure()
            ->with_starting_world(world_id, level_scene)->with_player(player_scene)->with_multiplayer(rules);
    GameConfig::from_definition(definition)->player_controls()->boot_server(this);
}

void DedicatedServerEntry::set_level_scene(const Ref<PackedScene> &value) { level_scene = value; }
Ref<PackedScene> DedicatedServerEntry::get_level_scene() const { return level_scene; }
void DedicatedServerEntry::set_player_scene(const Ref<PackedScene> &value) { player_scene = value; }
Ref<PackedScene> DedicatedServerEntry::get_player_scene() const { return player_scene; }
void DedicatedServerEntry::set_world_id(const StringName &value) { world_id = value; }
StringName DedicatedServerEntry::get_world_id() const { return world_id; }
void DedicatedServerEntry::set_rules(const Ref<MultiplayerRules> &value) { rules = value; }
Ref<MultiplayerRules> DedicatedServerEntry::get_rules() const { return rules; }
void DedicatedServerEntry::set_relay_between_guests(bool value) { relay_between_guests = value; }
bool DedicatedServerEntry::get_relay_between_guests() const { return relay_between_guests; }

Ref<Game> GameConfig::boot_server(Node *owner) const {
    auto *os = OS::get_singleton();
    if (!os || os->get_thread_caller_id() != os->get_main_thread_id() || !owner ||
            !owner->is_inside_tree() || owner->is_queued_for_deletion()) {
        return boot(owner);
    }
    const PackedStringArray args = os->get_cmdline_user_args();
    if (!args.has("--crimson-role=server")) {
        return boot(owner);
    }

    const auto reject_launch = [tree = owner->get_tree()](const String &message) {
        Ref<Game> result;
        result.instantiate();
        result->boot_outcome = GameResult::make(false, "GameBootFailed", message);
        DedicatedServerBoot::fail(*tree, message);
        return result;
    };
    if (online || coordinated || definition.is_null() || definition->get_multiplayer_rules().is_null() ||
            definition->get_service_provider().is_valid()) {
        return reject_launch("The local server launcher requires a fixed-world LAN profile.");
    }
    int64_t override_port = 0;
    String bind_address = "127.0.0.1";
    for (int64_t index = 0; index < args.size(); ++index) {
        const String argument = args[index];
        if (argument.begins_with("--crimson-port=")) {
            const String value = argument.trim_prefix("--crimson-port=");
            override_port = value.to_int();
            if (!value.is_valid_int() || override_port < 1 || override_port > 65535) {
                return reject_launch("The server port must be an integer from 1 to 65535.");
            }
        } else if (argument.begins_with("--crimson-bind=")) {
            bind_address = argument.trim_prefix("--crimson-bind=").strip_edges();
        }
    }
    if (bind_address.is_empty()) {
        return reject_launch("The server bind address cannot be empty.");
    }

    const ObjectID tree_id(owner->get_tree()->get_instance_id());
    Ref<Game> game = override_port == 0 ? boot(owner) : on_port(override_port)->boot(owner);
    auto *runtime = game->resolve_game();
    if (!runtime) {
        if (auto *tree = Object::cast_to<SceneTree>(ObjectDB::get_instance(tree_id))) {
            DedicatedServerBoot::fail(*tree, game->boot_result()->get_message());
        }
        return game;
    }
    auto *launcher = memnew(DedicatedServerBoot);
    launcher->game_id = ObjectID(runtime->get_instance_id());
    launcher->port = game->get_port();
    launcher->bind_address = bind_address;
    launcher->set_name("DedicatedLaunch");
    runtime->add_child(launcher, false, Node::INTERNAL_MODE_BACK);
    return game;
}

void DedicatedServerBoot::_bind_methods() {}

GameRuntime *DedicatedServerBoot::get_game() const {
    return Object::cast_to<GameRuntime>(ObjectDB::get_instance(game_id));
}

void DedicatedServerBoot::_ready() {
    callable_mp(this, &DedicatedServerBoot::start_server).call_deferred();
}

void DedicatedServerBoot::_exit_tree() {
    const Callable handler = callable_mp(this, &DedicatedServerBoot::on_started);
    if (starting.is_valid() && starting->is_connected("completed", handler)) {
        starting->disconnect("completed", handler);
    }
    starting.unref();
}

void DedicatedServerBoot::start_server() {
    if (!is_inside_tree()) {
        return;
    }
    GameRuntime *game = get_game();
    if (!game || !game->is_inside_tree() || game->get_tree() != get_tree() || game->is_queued_for_deletion()) {
        fail(*get_tree(), "The configured dedicated game is no longer available.");
        return;
    }
    starting = game->start_dedicated(bind_address);
    if (starting.is_null()) {
        fail(*get_tree(), "The dedicated game did not return a startup operation.");
    } else if (starting->is_finished()) {
        on_started(starting->get_result());
    } else {
        const Error connected = starting->connect("completed",
                callable_mp(this, &DedicatedServerBoot::on_started), CONNECT_ONE_SHOT);
        if (connected != OK) {
            starting.unref();
            fail(*get_tree(), "Could not observe dedicated startup completion (" + UtilityFunctions::error_string(connected) + String(")."));
        }
    }
}

void DedicatedServerBoot::on_started(const Dictionary &result) {
    starting.unref();
    if (!is_inside_tree()) {
        return;
    }
    if (!bool(result.get("ok", false))) {
        const String message = result.get("message", "");
        if (message.strip_edges().is_empty()) {
            const String code = result.get("code", "StartFailed");
            fail(*get_tree(), "The dedicated game could not start (" + code + String(")."));
        } else {
            fail(*get_tree(), message);
        }
        return;
    }
    GameRuntime *game = get_game();
    if (!game || game->get_phase() != GameRuntime::RUNNING || !game->get_world() || game->get_local_player()) {
        fail(*get_tree(), "The dedicated operation did not publish a playerless world.");
        return;
    }
    UtilityFunctions::print("[Crimson] READY server ", port);
}

void DedicatedServerBoot::fail(SceneTree &tree, const String &message) {
    UtilityFunctions::print("[Crimson] ERROR server ", message.replace("\r", " ").replace("\n", " "));
    tree.quit(1);
}

} // namespace crimson::integration
