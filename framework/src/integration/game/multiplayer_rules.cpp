// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#include "integration/game/multiplayer_rules.hpp"

#include <godot_cpp/core/class_db.hpp>

namespace crimson::integration {

using namespace godot;

void CrimsonMultiplayerRules::_bind_methods() {
    ClassDB::bind_static_method(get_class_static(), D_METHOD("listen_coop"), &CrimsonMultiplayerRules::listen_coop);
    ClassDB::bind_method(D_METHOD("with_max_players", "count"), &CrimsonMultiplayerRules::with_max_players);
    ClassDB::bind_method(D_METHOD("allow_join_in_progress", "enabled"), &CrimsonMultiplayerRules::allow_join_in_progress,
            DEFVAL(true));
    ClassDB::bind_method(D_METHOD("on_port", "port"), &CrimsonMultiplayerRules::on_port);

    ClassDB::bind_method(D_METHOD("set_max_players", "count"), &CrimsonMultiplayerRules::set_max_players);
    ClassDB::bind_method(D_METHOD("get_max_players"), &CrimsonMultiplayerRules::get_max_players);
    ClassDB::bind_method(D_METHOD("set_join_in_progress", "enabled"), &CrimsonMultiplayerRules::set_join_in_progress);
    ClassDB::bind_method(D_METHOD("get_join_in_progress"), &CrimsonMultiplayerRules::get_join_in_progress);
    ClassDB::bind_method(D_METHOD("set_port", "port"), &CrimsonMultiplayerRules::set_port);
    ClassDB::bind_method(D_METHOD("get_port"), &CrimsonMultiplayerRules::get_port);
    ADD_PROPERTY(PropertyInfo(Variant::INT, "max_players", PROPERTY_HINT_RANGE, "1,64,1"),
            "set_max_players", "get_max_players");
    ADD_PROPERTY(PropertyInfo(Variant::BOOL, "join_in_progress"), "set_join_in_progress", "get_join_in_progress");
    ADD_PROPERTY(PropertyInfo(Variant::INT, "port", PROPERTY_HINT_RANGE, "1,65535,1"), "set_port", "get_port");
}

Ref<CrimsonMultiplayerRules> CrimsonMultiplayerRules::listen_coop() {
    Ref<CrimsonMultiplayerRules> rules;
    rules.instantiate();
    return rules;
}

Ref<CrimsonMultiplayerRules> CrimsonMultiplayerRules::copy_rules() const {
    Ref<CrimsonMultiplayerRules> copy = listen_coop();
    copy->max_players = max_players;
    copy->join_in_progress = join_in_progress;
    copy->port = port;
    return copy;
}

Ref<CrimsonMultiplayerRules> CrimsonMultiplayerRules::with_max_players(int64_t value) const {
    Ref<CrimsonMultiplayerRules> copy = copy_rules();
    copy->set_max_players(value);
    return copy;
}

Ref<CrimsonMultiplayerRules> CrimsonMultiplayerRules::allow_join_in_progress(bool enabled) const {
    Ref<CrimsonMultiplayerRules> copy = copy_rules();
    copy->set_join_in_progress(enabled);
    return copy;
}

Ref<CrimsonMultiplayerRules> CrimsonMultiplayerRules::on_port(int64_t value) const {
    Ref<CrimsonMultiplayerRules> copy = copy_rules();
    copy->set_port(value);
    return copy;
}

void CrimsonMultiplayerRules::set_max_players(int64_t value) {
    max_players = value;
}

int64_t CrimsonMultiplayerRules::get_max_players() const {
    return max_players;
}

void CrimsonMultiplayerRules::set_join_in_progress(bool enabled) {
    join_in_progress = enabled;
}

bool CrimsonMultiplayerRules::get_join_in_progress() const {
    return join_in_progress;
}

void CrimsonMultiplayerRules::set_port(int64_t value) {
    port = value;
}

int64_t CrimsonMultiplayerRules::get_port() const {
    return port;
}

} // namespace crimson::integration
