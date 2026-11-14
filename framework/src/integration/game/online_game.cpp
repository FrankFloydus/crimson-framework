// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#include "integration/game/crimson_game.hpp"
#include "integration/online/steam/steam_dedicated.hpp"
#include "integration/online/steam/steam_online.hpp"

#include <godot_cpp/classes/time.hpp>

#include <utility>

using namespace godot;

namespace crimson::integration {
namespace {

// Bound one provider request so a stalled directory cannot hold a world indefinitely.
constexpr uint64_t directory_timeout_ms = 15000;
// Allow the existing 22-second Steam search to settle before retiring this game operation.
constexpr uint64_t discovery_timeout_ms = 23000;
// Bound provider data inspected before selecting a single quick-join candidate.
constexpr int64_t quick_join_limit = 12;

// Directory work is consumed by the scene thread against a monotonic deadline.
uint64_t directory_deadline() {
    return Time::get_singleton()->get_ticks_msec() + directory_timeout_ms;
}

// Prefer the captured cause over a generic setup instruction for a failed Steam attachment.
String online_start_error(const Ref<OnlineDirectory> &binding) {
    if (const Ref<SteamOnlineDirectory> listen(binding); listen.is_valid()) {
        const auto scope = listen->get_steam_scope();
        if (scope.is_valid()) {
            const String detail = scope->get_initialization_error();
            return detail.is_empty() ? String("The Steam game connection is unavailable. Check the Steam client and relaunch the game.") : detail;
        }
    }
    if (const Ref<SteamDedicatedOnlineDirectory> dedicated(binding); dedicated.is_valid()) {
        const auto server = dedicated->service();
        if (server.is_valid()) {
            const String detail = server->get_initialization_error();
            return detail.is_empty() ? String("The Steam dedicated connection is unavailable. Check the server configuration.") : detail;
        }
    }
    return "Configure an online directory before starting an online game.";
}

} // namespace

Ref<GameOperation> GameRuntime::host_online(const String &public_address, const String &bind_address) {
    return begin_online_host(ImmediateMode::Host, public_address, bind_address);
}

Ref<GameOperation> GameRuntime::start_dedicated_online(const String &public_address, const String &bind_address) {
    return begin_online_host(ImmediateMode::Dedicated, public_address, bind_address);
}

bool GameRuntime::can_start_session() const {
    return session_node && is_inside_tree() && phase == IDLE && !immediate_active && !cleaning &&
            !publishing && online_start.operation.is_null() &&
            session_node->get_state() == session::GameSession::IDLE;
}

Ref<GameOperation> GameRuntime::check_online_start() const {
    if (!session_node || !is_inside_tree() || definition.is_null() || online_directory.is_null() ||
            !online_directory->is_game_available()) {
        return completed(false, "OnlineUnavailable", online_start_error(GameServiceContainer::online_for(this)));
    }
    if (!can_start_session()) {
        return completed(false, "GameBusy", "Leave the current game before starting another.");
    }
    if (session_node->get_authenticator().is_null()) {
        return completed(false, "OnlineAuthenticationRequired", "Configure peer authentication for online games.");
    }
    return {};
}

Ref<GameOperation> GameRuntime::begin_online_host(
        ImmediateMode mode, const String &public_address, const String &bind_address) {
    if (const auto rejected = check_online_start(); rejected.is_valid()) {
        return rejected;
    }
    if (!online_directory->supports_host_mode(mode == ImmediateMode::Dedicated)) {
        return completed(false, "OnlineModeUnavailable", "The selected online provider does not support this hosting mode.");
    }
    const Ref<MultiplayerRules> rules = definition->get_multiplayer_rules();
    if (rules.is_null()) {
        return completed(false, "MultiplayerDisabled", "Configure multiplayer rules before hosting online.");
    }
    const String address = public_address.strip_edges();
    const auto endpoint = session::SessionEndpoint::direct(address, rules->get_port());
    if (!address.is_empty() && (endpoint.is_null() || address == "*" || address == "0.0.0.0" || address == "::" ||
            address.length() > 255)) {
        return completed(false, "InvalidAddress", "Advertise a host address guests can connect to.");
    }
    Ref<GameOperation> operation;
    operation.instantiate();
    online_start.operation = operation;
    online_start.authenticator = session_node->get_authenticator();
    online_start.publication = endpoint;
    online_start.hosting = true;
    const auto world_start = start_immediate(mode, bind_address.strip_edges(), {}, true);
    if (online_start.operation == operation) {
        online_start.world_start = world_start;
    }
    return operation;
}

Ref<GameOperation> GameRuntime::join_listing(const Ref<OnlineListing> &listing) {
    if (const auto rejected = check_online_start(); rejected.is_valid()) {
        return rejected;
    }
    if (listing.is_null() || !online_directory->accepts_listing(listing)) {
        return completed(false, "InvalidListing", "Select a listing from this game's online provider.");
    }
    return join_online(listing->get_id());
}

Ref<GameOperation> GameRuntime::join_online(const String &listing_id) {
    if (const auto rejected = check_online_start(); rejected.is_valid()) {
        return rejected;
    }
    const String id = listing_id.strip_edges();
    if (id.is_empty() || id.length() > 192) {
        return completed(false, "InvalidListing", "Enter a valid online listing ID.");
    }
    Ref<GameOperation> operation;
    operation.instantiate();
    online_start.operation = operation;
    online_start.authenticator = session_node->get_authenticator();
    resolve_online_join(id);
    return operation;
}

void GameRuntime::resolve_online_join(const String &id) {
    const auto operation = online_start.operation;
    online_start.deadline = directory_deadline();
    online_join = id;
    const auto directory = online_directory;
    const auto request = directory->resolve(id);
    if (online_start.operation == operation) {
        online_start.request = request;
    } else {
        request->cancel();
    }
}

Ref<GameOperation> GameRuntime::find_and_join_online() {
    if (const auto rejected = check_online_start(); rejected.is_valid()) {
        return rejected;
    }
    const auto scope = SteamServices::for_game(this);
    const auto lobbies = scope.is_valid() ? scope->lobbies() : Ref<steam::SteamLobbies>();
    if (lobbies.is_null() || !lobbies->is_available()) {
        return completed(false, "DiscoveryUnavailable", "The selected connection does not support finding games.");
    }
    const StringName world = definition->get_starting_world();
    if (world.is_empty()) {
        return completed(false, "InvalidWorld", "Configure the starting world before finding games.");
    }
    const auto query = lobbies->query()->on_map(world)->with_room_for(1)->limit(quick_join_limit);
    Ref<GameOperation> operation;
    operation.instantiate();
    online_start.operation = operation;
    online_start.authenticator = session_node->get_authenticator();
    online_start.deadline = Time::get_singleton()->get_ticks_msec() + discovery_timeout_ms;
    const auto search = query->fetch_operation();
    if (online_start.operation == operation) {
        online_start.search = search;
    } else {
        search->cancel();
    }
    return operation;
}

void GameRuntime::process_online_search() {
    const auto search = online_start.search;
    if (Time::get_singleton()->get_ticks_msec() >= online_start.deadline) {
        fail_online_start(outcome(false, "DiscoveryTimeout", "Steam did not finish searching for games in time."));
        return;
    }
    if (!search->is_finished()) {
        return;
    }
    const auto result = search->result();
    if (!result->is_ok()) {
        const Error error = result->get_error();
        const char *code = error == ERR_BUSY ? "DiscoveryBusy" : error == ERR_TIMEOUT ? "DiscoveryTimeout" :
                error == ERR_UNAVAILABLE ? "DiscoveryUnavailable" : "DiscoveryFailed";
        const String message = error == ERR_BUSY ?
                String("Steam is already searching or clearing a previous search. Try again shortly.") :
                String("Steam could not finish searching for games. ") + result->get_message();
        fail_online_start(outcome(false, code, message));
        return;
    }
    const auto matches = result->get_value();
    if (matches.is_empty()) {
        fail_online_start(outcome(false, "NoMatches", "No compatible Steam games with open seats were found."));
        return;
    }
    const Ref<OnlineListing> listing = matches[0];
    if (listing.is_null() || !online_directory->accepts_listing(listing)) {
        fail_online_start(outcome(false, "ListingUnavailable", "The discovered listing no longer belongs to this game."));
        return;
    }
    online_start.search.unref();
    resolve_online_join(listing->get_id());
}

String GameRuntime::get_online_listing() const {
    return immediate_active && phase == RUNNING ? online_listing : String();
}

void GameRuntime::process_online_start() {
    if (online_start.search.is_valid()) {
        process_online_search();
        return;
    }
    const auto operation = online_start.operation;
    if (online_start.world_start.is_valid()) {
        if (!online_start.world_start->is_finished()) {
            return;
        }
        const Dictionary result = online_start.world_start->get_result();
        if (!bool(result.get("ok", false))) {
            fail_online_start(result);
            return;
        }
        if (!online_start.hosting) {
            finish_online_start();
            return;
        }
        if (online_start.request.is_null()) {
            online_start.deadline = directory_deadline();
            const auto directory = online_directory;
            const auto endpoint = online_start.publication;
            const auto request = directory->publish(endpoint, world_id);
            if (online_start.operation == operation) {
                online_start.request = request;
            } else {
                request->cancel();
            }
            return;
        }
    }

    const auto request = online_start.request;
    if (Time::get_singleton()->get_ticks_msec() >= online_start.deadline) {
        fail_online_start(outcome(false, "DirectoryTimeout", "The online directory did not respond."));
        return;
    }
    if (request.is_null() || request->state != OnlineDirectoryRequest::State::Finished) {
        return;
    }
    if (online_start.hosting) {
        online_listing = request->take_listing();
        if (online_listing.is_empty()) {
            fail_online_start(outcome(false, "ListingPublishFailed", "The online directory could not publish this game."));
            return;
        }
        const Error admission = session_node->set_accepting_connections(true);
        if (online_start.operation != operation) {
            return;
        }
        if (admission != OK) {
            fail_online_start(outcome(false, "StartFailed", "The published game could not open admission."));
        } else {
            finish_online_start();
        }
        return;
    }

    const auto endpoint = request->endpoint;
    if (endpoint.is_null()) {
        fail_online_start(outcome(false, "ListingUnavailable", "The online listing could not be resolved."));
        return;
    }
    online_start.request.unref();
    online_start.deadline = 0;
    const auto world_start = start_immediate(ImmediateMode::Join, {}, endpoint, true);
    if (online_start.operation == operation) {
        online_start.world_start = world_start;
    }
}

Ref<GameOperation> GameRuntime::retire_online_start() {
    const OnlineStart retired = std::exchange(online_start, {});
    if (retired.request.is_valid()) {
        retired.request->cancel();
    }
    if (retired.search.is_valid() && !retired.search->is_finished()) {
        retired.search->cancel();
    }
    return retired.operation;
}

void GameRuntime::retire_online_presence() {
    const String listing = std::exchange(online_listing, String());
    const String joined = std::exchange(online_join, String());
    const auto directory = online_directory;
    if (!listing.is_empty() && directory.is_valid()) {
        directory->unpublish(listing);
    }
    if (!joined.is_empty() && directory.is_valid()) {
        directory->leave_listing(joined);
    }
}

void GameRuntime::fail_online_start(const Dictionary &result) {
    publishing = true;
    const auto operation = retire_online_start();
    retire_online_presence();
    publishing = false;
    if (immediate_active) {
        immediate->leave();
    }
    operation->finish(result);
}

void GameRuntime::finish_online_start() {
    const auto operation = std::exchange(online_start, {}).operation;
    const int64_t ready_epoch = world_epoch;
    Dictionary result = outcome(true, "Ok");
    if (!online_listing.is_empty()) {
        result["listing_id"] = online_listing;
    }
    set_phase(RUNNING);
    if (immediate_active && phase == RUNNING && world_epoch == ready_epoch) {
        emit_signal("world_ready", world_id, world_epoch);
    }
    operation->finish(result);
}

} // namespace crimson::integration
