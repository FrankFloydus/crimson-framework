// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#include "integration/game/multiplayer_rules.hpp"

#include <godot_cpp/core/class_db.hpp>

namespace crimson::integration {

using namespace godot;

void MultiplayerRules::_bind_methods() {
    ClassDB::bind_static_method(get_class_static(), D_METHOD("listen_coop"), &MultiplayerRules::listen_coop);
    ClassDB::bind_method(D_METHOD("with_max_players", "count"), &MultiplayerRules::with_max_players);
    ClassDB::bind_method(D_METHOD("allow_join_in_progress", "enabled"), &MultiplayerRules::allow_join_in_progress,
            DEFVAL(true));
    ClassDB::bind_method(D_METHOD("on_port", "port"), &MultiplayerRules::on_port);

    ClassDB::bind_method(D_METHOD("set_max_players", "count"), &MultiplayerRules::set_max_players);
    ClassDB::bind_method(D_METHOD("get_max_players"), &MultiplayerRules::get_max_players);
    ClassDB::bind_method(D_METHOD("set_join_in_progress", "enabled"), &MultiplayerRules::set_join_in_progress);
    ClassDB::bind_method(D_METHOD("get_join_in_progress"), &MultiplayerRules::get_join_in_progress);
    ClassDB::bind_method(D_METHOD("set_port", "port"), &MultiplayerRules::set_port);
    ClassDB::bind_method(D_METHOD("get_port"), &MultiplayerRules::get_port);
    ADD_PROPERTY(PropertyInfo(Variant::INT, "max_players", PROPERTY_HINT_RANGE, "1,64,1"),
            "set_max_players", "get_max_players");
    ADD_PROPERTY(PropertyInfo(Variant::BOOL, "join_in_progress"), "set_join_in_progress", "get_join_in_progress");
    ADD_PROPERTY(PropertyInfo(Variant::INT, "port", PROPERTY_HINT_RANGE, "1,65535,1"), "set_port", "get_port");
}

Ref<MultiplayerRules> MultiplayerRules::listen_coop() {
    Ref<MultiplayerRules> rules;
    rules.instantiate();
    return rules;
}

Ref<MultiplayerRules> MultiplayerRules::copy_rules() const {
    Ref<MultiplayerRules> copy = listen_coop();
    copy->max_players = max_players;
    copy->join_in_progress = join_in_progress;
    copy->port = port;
    return copy;
}

Ref<MultiplayerRules> MultiplayerRules::with_max_players(int64_t value) const {
    Ref<MultiplayerRules> copy = copy_rules();
    copy->set_max_players(value);
    return copy;
}

Ref<MultiplayerRules> MultiplayerRules::allow_join_in_progress(bool enabled) const {
    Ref<MultiplayerRules> copy = copy_rules();
    copy->set_join_in_progress(enabled);
    return copy;
}

Ref<MultiplayerRules> MultiplayerRules::on_port(int64_t value) const {
    Ref<MultiplayerRules> copy = copy_rules();
    copy->set_port(value);
    return copy;
}

void MultiplayerRules::set_max_players(int64_t value) {
    max_players = value;
}

int64_t MultiplayerRules::get_max_players() const {
    return max_players;
}

void MultiplayerRules::set_join_in_progress(bool enabled) {
    join_in_progress = enabled;
}

bool MultiplayerRules::get_join_in_progress() const {
    return join_in_progress;
}

void MultiplayerRules::set_port(int64_t value) {
    port = value;
}

int64_t MultiplayerRules::get_port() const {
    return port;
}

} // namespace crimson::integration
