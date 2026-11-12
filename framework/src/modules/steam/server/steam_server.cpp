// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#include "modules/steam/server/steam_server.hpp"
#include "modules/steam/lobbies/steam_lobbies.hpp"

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>

namespace crimson::steam {
using namespace godot;

namespace {

// Reject empty text, controls, and UTF-8 strings too large for the selected Steam field.
bool valid_server_text(const String &name, int64_t maximum_bytes = 63) {
    if (name.is_empty() || name.length() > maximum_bytes || name.utf8().length() > maximum_bytes) {
        return false;
    }
    for (int64_t index = 0; index < name.length(); ++index) {
        if (name[index] < U' ' || name[index] == 127) {
            return false;
        }
    }
    return true;
}

// Accept one install folder name, never an absolute or relative path.
bool valid_install_folder(const String &folder) {
    if (!valid_server_text(folder, 31) || folder != folder.strip_edges() || folder.ends_with(".")) {
        return false;
    }
    constexpr const char *invalid_characters = "<>:\"/\\|?*";
    for (const char *character = invalid_characters; *character; ++character) {
        if (folder.contains(String::chr(*character))) {
            return false;
        }
    }
    return true;
}

// Accept only an explicit operator environment key, never a login token.
bool valid_login_ref(const String &key) {
    if (key.length() > 64) {
        return false;
    }
    for (int64_t index = 0; index < key.length(); ++index) {
        const char32_t c = key[index];
        if (!((c >= U'A' && c <= U'Z') || c == U'_' || (index != 0 && c >= U'0' && c <= U'9'))) {
            return false;
        }
    }
    return true;
}

} // namespace

void SteamServer::_bind_methods() {
    ClassDB::bind_static_method(get_class_static(), D_METHOD("configure", "name", "game_port", "query_port"),
            &SteamServer::configure, DEFVAL("Crimson Server"), DEFVAL(27015), DEFVAL(27016));
    ClassDB::bind_method(D_METHOD("secure"), &SteamServer::secure);
    ClassDB::bind_method(D_METHOD("bind_to", "address"), &SteamServer::bind_to);
    ClassDB::bind_method(D_METHOD("advertise_on", "scope"), &SteamServer::advertise_on);
    ClassDB::bind_method(D_METHOD("with_login", "secret_ref"), &SteamServer::with_login);
    ClassDB::bind_method(D_METHOD("with_product_metadata", "version", "description", "install_dir"),
            &SteamServer::with_product_metadata);
    ClassDB::bind_method(D_METHOD("get_product_version"), &SteamServer::get_product_version);
    ClassDB::bind_method(D_METHOD("get_game_description"), &SteamServer::get_game_description);
    ClassDB::bind_method(D_METHOD("get_install_dir"), &SteamServer::get_install_dir);
    ClassDB::bind_method(D_METHOD("is_valid"), &SteamServer::is_valid);
    ClassDB::bind_method(D_METHOD("via_sdr", "profile"), &SteamServer::via_sdr);
    BIND_ENUM_CONSTANT(DIRECT);
    BIND_ENUM_CONSTANT(LAN);
    BIND_ENUM_CONSTANT(INTERNET);
}

Ref<SteamServer> SteamServer::configure(const String &name, int64_t gameplay, int64_t query) {
    Ref<SteamServer> options;
    options.instantiate();
    options->valid = valid_server_text(name) && gameplay >= 1 && gameplay <= 65535 &&
            query >= 1 && query <= 65534 && gameplay != query;
    if (options->valid) {
        options->server_name = name;
        options->game_port = static_cast<uint16_t>(gameplay);
        options->query_port = static_cast<uint16_t>(query);
    }
    return options;
}

Ref<SteamServer> SteamServer::branch() const {
    Ref<SteamServer> next;
    next.instantiate();
    next->server_name = server_name;
    next->bind_ipv4 = bind_ipv4;
    next->game_port = game_port;
    next->query_port = query_port;
    next->advertisement = advertisement;
    next->login_ref = login_ref;
    next->product_version = product_version;
    next->game_description = game_description;
    next->install_dir = install_dir;
    next->relay_profile = relay_profile;
    next->valid = valid;
    return next;
}

Ref<SteamServer> SteamServer::secure() const {
    return branch();
}

Ref<SteamServer> SteamServer::bind_to(const String &address) const {
    const auto next = branch();
    uint32_t parsed = 0;
    next->valid = valid && (address == "*" || parse_ipv4(address, parsed));
    if (next->valid) {
        next->bind_ipv4 = parsed;
    }
    return next;
}

Ref<SteamServer> SteamServer::advertise_on(Advertisement scope) const {
    const auto next = branch();
    next->valid = valid && (scope == DIRECT || scope == LAN || scope == INTERNET);
    if (next->valid) {
        next->advertisement = scope;
    }
    return next;
}

Ref<SteamServer> SteamServer::with_login(const StringName &secret_ref) const {
    const auto next = branch();
    next->valid = valid && valid_login_ref(String(secret_ref));
    if (next->valid) {
        next->login_ref = secret_ref;
    }
    return next;
}

Ref<SteamServer> SteamServer::with_product_metadata(const String &version, const String &description,
        const String &folder) const {
    const auto next = branch();
    next->valid = valid && valid_server_text(version) && valid_server_text(description) &&
            valid_install_folder(folder);
    if (next->valid) {
        next->product_version = version;
        next->game_description = description;
        next->install_dir = folder;
    }
    return next;
}

Ref<SteamServer> SteamServer::via_sdr(const StringName &profile) const {
    const auto next = branch();
    const String name(profile);
    next->valid = valid && (profile.is_empty() || (name.length() <= 32 && valid_steam_lobby_namespace(name)));
    if (next->valid) {
        next->relay_profile = profile;
    }
    return next;
}

bool SteamServer::is_valid() const { return valid; }
String SteamServer::get_server_name() const { return server_name; }
uint32_t SteamServer::get_bind_ipv4() const { return bind_ipv4; }
uint16_t SteamServer::get_game_port() const { return game_port; }
uint16_t SteamServer::get_query_port() const { return query_port; }
SteamServer::Advertisement SteamServer::get_advertisement() const { return advertisement; }
StringName SteamServer::get_login_ref() const { return login_ref; }
String SteamServer::get_product_version() const { return product_version; }
String SteamServer::get_game_description() const { return game_description; }
String SteamServer::get_install_dir() const { return install_dir; }
StringName SteamServer::get_relay_profile() const { return relay_profile; }

bool SteamServer::parse_ipv4(const String &text, uint32_t &address) {
    if (text.length() < 7 || text.length() > 15) {
        return false;
    }
    const auto parts = text.split(".", true);
    if (parts.size() != 4) {
        return false;
    }
    uint32_t result = 0;
    for (int64_t index = 0; index < parts.size(); ++index) {
        const String part = parts[index];
        if (part.is_empty() || part.length() > 3 || (part.length() > 1 && part[0] == U'0')) {
            return false;
        }
        uint32_t octet = 0;
        for (int64_t character = 0; character < part.length(); ++character) {
            const char32_t digit = part[character];
            if (digit < U'0' || digit > U'9') {
                return false;
            }
            octet = octet * 10 + static_cast<uint32_t>(digit - U'0');
        }
        if (octet > 255) {
            return false;
        }
        result = (result << 8) | octet;
    }
    address = result;
    return true;
}

} // namespace crimson::steam
