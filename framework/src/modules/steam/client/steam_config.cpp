// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#include "modules/steam/client/steam_config.hpp"

#include "modules/steam/lobbies/steam_lobbies.hpp"

#include <godot_cpp/core/class_db.hpp>

#include <limits>

namespace crimson::steam {
using namespace godot;

void SteamConfig::_bind_methods() {
    ClassDB::bind_static_method(get_class_static(), D_METHOD("spacewar", "group"), &SteamConfig::spacewar);
    ClassDB::bind_method(D_METHOD("public_games"), &SteamConfig::public_games);
    ClassDB::bind_method(D_METHOD("is_valid"), &SteamConfig::is_valid);
    ClassDB::bind_method(D_METHOD("set_app_id", "app_id"), &SteamConfig::set_app_id);
    ClassDB::bind_method(D_METHOD("get_app_id"), &SteamConfig::get_app_id);
    ClassDB::bind_method(D_METHOD("set_test_group", "group"), &SteamConfig::set_test_group);
    ClassDB::bind_method(D_METHOD("get_test_group"), &SteamConfig::get_test_group);
    ClassDB::bind_method(D_METHOD("set_public", "enabled"), &SteamConfig::set_public);
    ClassDB::bind_method(D_METHOD("is_public"), &SteamConfig::is_public);
    ADD_PROPERTY(PropertyInfo(Variant::INT, "app_id", PROPERTY_HINT_RANGE, "0,4294967295,1"),
            "set_app_id", "get_app_id");
    ADD_PROPERTY(PropertyInfo(Variant::STRING, "test_group"), "set_test_group", "get_test_group");
    ADD_PROPERTY(PropertyInfo(Variant::BOOL, "discoverable"), "set_public", "is_public");
}

Ref<SteamConfig> SteamConfig::spacewar(const String &group) {
    Ref<SteamConfig> result;
    result.instantiate();
    result->app_id = 480;
    result->test_group = group;
    return result;
}

Ref<SteamConfig> SteamConfig::public_games() const {
    Ref<SteamConfig> result;
    result.instantiate();
    result->app_id = app_id;
    result->test_group = test_group;
    result->discoverable = true;
    return result;
}

bool SteamConfig::is_valid() const {
    return app_id >= 0 && app_id <= (std::numeric_limits<uint32_t>::max)() &&
            (app_id == 480 ? valid_steam_lobby_namespace(test_group) : test_group.is_empty());
}

void SteamConfig::set_app_id(int64_t value) { app_id = value; }
int64_t SteamConfig::get_app_id() const { return app_id; }
void SteamConfig::set_test_group(const String &value) { test_group = value; }
String SteamConfig::get_test_group() const { return test_group; }
void SteamConfig::set_public(bool enabled) { discoverable = enabled; }
bool SteamConfig::is_public() const { return discoverable; }

} // namespace crimson::steam
