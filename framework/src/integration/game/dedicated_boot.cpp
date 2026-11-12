// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#include "integration/game/dedicated_boot.hpp"

#include "integration/game/crimson_game.hpp"

#include <godot_cpp/classes/os.hpp>
#include <godot_cpp/classes/scene_tree.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/callable_method_pointer.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

namespace crimson::integration {

using namespace godot;

void CrimsonDedicatedBoot::_bind_methods() {
    ClassDB::bind_method(D_METHOD("set_game", "game"), &CrimsonDedicatedBoot::set_game);
    ClassDB::bind_method(D_METHOD("get_game"), &CrimsonDedicatedBoot::get_game);
    ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "game", PROPERTY_HINT_NODE_TYPE, "CrimsonGame"), "set_game", "get_game");
}

void CrimsonDedicatedBoot::set_game(CrimsonGame *game) {
    game_id = game ? ObjectID(game->get_instance_id()) : ObjectID();
}

CrimsonGame *CrimsonDedicatedBoot::get_game() const {
    return Object::cast_to<CrimsonGame>(ObjectDB::get_instance(game_id));
}

void CrimsonDedicatedBoot::_ready() {
    if (!get_tree()->get_edited_scene_root() &&
            OS::get_singleton()->get_cmdline_user_args().has("--crimson-role=server")) {
        callable_mp(this, &CrimsonDedicatedBoot::start_server).call_deferred();
    }
}

void CrimsonDedicatedBoot::_exit_tree() {
    const Callable handler = callable_mp(this, &CrimsonDedicatedBoot::on_started);
    if (starting.is_valid() && starting->is_connected("completed", handler)) {
        starting->disconnect("completed", handler);
    }
    starting.unref();
}

void CrimsonDedicatedBoot::start_server() {
    if (!is_inside_tree()) {
        return;
    }
    CrimsonGame *game = get_game();
    const Ref<CrimsonGameDefinition> definition = game ? game->get_definition() : Ref<CrimsonGameDefinition>();
    const Ref<CrimsonMultiplayerRules> rules = definition.is_valid() ? definition->get_multiplayer_rules() :
            Ref<CrimsonMultiplayerRules>();
    if (rules.is_null() || !game->is_inside_tree() || game->get_tree() != get_tree()) {
        fail("Assign a CrimsonGame in the server scene with multiplayer rules.");
        return;
    }

    port = rules->get_port();
    String bind_address = "127.0.0.1";
    const PackedStringArray args = OS::get_singleton()->get_cmdline_user_args();
    for (int64_t index = 0; index < args.size(); ++index) {
        const String argument = args[index];
        if (argument.begins_with("--crimson-port=")) {
            const String value = argument.trim_prefix("--crimson-port=");
            if (!value.is_valid_int() || value.to_int() < 1 || value.to_int() > 65535) {
                fail("The server port must be an integer from 1 to 65535.");
                return;
            }
            port = value.to_int();
        } else if (argument.begins_with("--crimson-bind=")) {
            bind_address = argument.trim_prefix("--crimson-bind=").strip_edges();
        }
    }
    if (bind_address.is_empty()) {
        fail("The server bind address cannot be empty.");
        return;
    }

    game->set_definition(definition->with_multiplayer(rules->on_port(port)));
    starting = game->start_dedicated(bind_address);
    if (starting->is_finished()) {
        on_started(starting->get_result());
    } else {
        starting->connect("completed", callable_mp(this, &CrimsonDedicatedBoot::on_started), CONNECT_ONE_SHOT);
    }
}

void CrimsonDedicatedBoot::on_started(const Dictionary &result) {
    starting.unref();
    if (!is_inside_tree()) {
        return;
    }
    if (!bool(result.get("ok", false))) {
        fail(result.get("message", "The dedicated game could not start."));
        return;
    }
    CrimsonGame *game = get_game();
    if (!game || game->get_phase() != CrimsonGame::RUNNING || !game->get_world() || game->get_local_player()) {
        fail("The dedicated operation did not publish a playerless world.");
        return;
    }
    UtilityFunctions::print("[Crimson] READY server ", port);
}

void CrimsonDedicatedBoot::fail(const String &message) {
    UtilityFunctions::print("[Crimson] ERROR server ", message);
    get_tree()->quit(1);
}

} // namespace crimson::integration
