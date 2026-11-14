// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#include "integration/game/game.hpp"

#include "integration/game/crimson_game.hpp"
#include "integration/online/steam/steam_online.hpp"

#include <godot_cpp/variant/callable_method_pointer.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

using namespace godot;

namespace crimson::integration {
namespace {

// Explain an unavailable invitation action using the exact attached Steam startup diagnostic.
String invite_unavailable_message(GameRuntime &game) {
    if (const auto steam = SteamServices::for_game(&game); steam.is_valid()) {
        const String detail = steam->get_initialization_error();
        if (!detail.is_empty()) {
            return detail;
        }
    }
    return "Steam invitations are unavailable for this game. Select Steam and check the signed-in client.";
}

} // namespace

Variant Game::host(const String &bind_address, const String &public_address) {
    auto *game = resolve_game();
    if (!game) {
        return reject("GameUnavailable", "Boot a game before hosting.");
    }
    if (coordinated) {
        return reject("CoordinatedSessionRequired", "Use the coordinated lobby flow to bind players and start this match.");
    }
    return await_operation(uses_online() ? game->host_online(public_address, bind_address) :
                                           game->host_game(bind_address));
}

Variant Game::join(const String &address) {
    auto *game = resolve_game();
    if (!game) {
        return reject("GameUnavailable", "Boot a game before joining.");
    }
    if (uses_online()) {
        return reject("AddressJoinUnavailable", "Use a Steam invitation, a selected listing, or Find a Game with this connection.");
    }
    if (coordinated) {
        return reject("CoordinatedSessionRequired", "Use the coordinated lobby flow to join and bind a player for this match.");
    }
    return await_operation(game->join_game(address));
}

Variant Game::serve(const String &bind_address, const String &public_address) {
    auto *game = resolve_game();
    if (!game) {
        return reject("GameUnavailable", "Boot a game before starting a dedicated server.");
    }
    if (coordinated) {
        return reject("CoordinatedSessionRequired", "Use the coordinated lobby flow to start a server for this match.");
    }
    return await_operation(uses_online() ? game->start_dedicated_online(public_address, bind_address) :
                                           game->start_dedicated(bind_address));
}

Variant Game::find_and_join() {
    auto *game = resolve_game();
    if (!game) {
        return reject("GameUnavailable", "Boot a game before searching for an online match.");
    }
    if (!uses_online() || coordinated) {
        return reject("DiscoveryUnavailable", "Choose Steam for this game's connection to find a playable match.");
    }
    return await_operation(game->find_and_join_online());
}

Variant Game::join_listing(const Ref<OnlineListing> &listing) {
    auto *game = resolve_game();
    if (!game) {
        return reject("GameUnavailable", "Boot a game before joining a listing.");
    }
    if (coordinated) {
        return reject("CoordinatedSessionRequired", "Use the coordinated lobby flow to join and bind a player for this match.");
    }
    return await_operation(game->join_listing(listing));
}

Ref<GameResult> Game::open_lobby(const String &bind_address) {
    auto *runtime = resolve_game();
    if (!runtime) {
        return reject("GameUnavailable", "Boot a game before opening a lobby.");
    }
    const auto rules = runtime->get_definition()->get_multiplayer_rules();
    if (!coordinated || uses_online() || rules.is_null()) {
        return reject("LobbyUnavailable", "Choose a coordinated LAN game before opening a lobby.");
    }
    if (!runtime->can_start_session()) {
        return reject("GameBusy", "Leave the current session before opening a lobby.");
    }
    const Ref<Game> keep_alive(this);
    if (prepare_authority() != OK) {
        return reject("InventoryUnavailable", "The game's inventory could not prepare its authority store.");
    }
    runtime = resolve_game();
    if (!runtime || !runtime->can_start_session()) {
        return reject("GameBusy", "The game changed while preparing the lobby.");
    }
    const Error error = runtime->host(rules->get_port(), rules->get_max_players() - 1, bind_address.strip_edges());
    return error == OK ? GameResult::make(true, "Ok", "The lobby is accepting connections.") :
            reject("LobbyOpenFailed", "Could not open the lobby: " + UtilityFunctions::error_string(error) + ".");
}

Ref<GameResult> Game::join_lobby(const String &address) {
    auto *runtime = resolve_game();
    if (!runtime) {
        return reject("GameUnavailable", "Boot a game before joining a lobby.");
    }
    const auto rules = runtime->get_definition()->get_multiplayer_rules();
    if (!coordinated || uses_online() || rules.is_null()) {
        return reject("LobbyUnavailable", "Choose a coordinated LAN game before joining a lobby.");
    }
    if (!runtime->can_start_session()) {
        return reject("GameBusy", "Leave the current session before joining a lobby.");
    }
    const Ref<Game> keep_alive(this);
    const Error error = runtime->join(address.strip_edges(), rules->get_port());
    return error == OK ? GameResult::make(true, "Ok", "The lobby connection is pending admission.") :
            reject("LobbyJoinFailed", "Could not start the lobby connection: " + UtilityFunctions::error_string(error) + ".");
}

Error Game::bind_player(int64_t peer_id, const String &player_id) {
    auto *runtime = resolve_game();
    return runtime && coordinated ? runtime->bind_player(peer_id, player_id) : ERR_UNAVAILABLE;
}

Variant Game::start_match(const StringName &world) {
    auto *runtime = resolve_game();
    if (!runtime || !coordinated) {
        return reject("LobbyUnavailable", "Boot a coordinated game before starting a match.");
    }
    return await_operation(runtime->start_match(world));
}

void Game::on_peer_joined(int64_t peer_id) {
    if (coordinated) {
        const Ref<Game> keep_alive(this);
        emit_signal("peer_joined", peer_id);
    }
}

Ref<GameResult> Game::invite_friends() {
    auto *game = resolve_game();
    if (!game) {
        return reject("GameUnavailable", "Boot a game before opening invitations.");
    }
    const auto gateway = game->invites();
    if (gateway.is_null() || !gateway->is_available()) {
        return reject("InvitesUnavailable", invite_unavailable_message(*game));
    }
    const Error error = gateway->open_overlay();
    if (error == OK) {
        return GameResult::make(true, "Ok", "Steam accepted the invite picker request.");
    }
    if (error == ERR_BUSY) {
        return reject("OverlayBusy", "The Steam overlay is still loading. Try again when it is ready.");
    }
    if (error == ERR_UNAUTHORIZED) {
        return reject("InviteNotHost", "Only this Steam lobby's current host can open its invite picker.");
    }
    return reject("InvitesUnavailable", "Host a Steam game and enable the Steam overlay before inviting friends.");
}

Variant Game::accept_invite(const Ref<SteamInvite> &invite) {
    auto *game = resolve_game();
    if (!game) {
        return reject("GameUnavailable", "Boot a game before accepting an invitation.");
    }
    if (coordinated) {
        return reject("CoordinatedSessionRequired", "Use the coordinated lobby flow to join and bind a player for this match.");
    }
    const auto gateway = game->invites();
    if (gateway.is_null() || !gateway->is_available()) {
        return reject("InvitesUnavailable", invite_unavailable_message(*game));
    }
    if (invite.is_null() || gateway->pending() != invite) {
        return reject("InviteChanged", "This invitation is no longer pending. Confirm the current invitation instead.");
    }
    const auto operation = game->join_listing(invite->get_listing());
    if (!operation->is_finished() || bool(operation->get_result().get("ok", false))) {
        gateway->discard(invite);
    }
    return await_operation(operation);
}

Ref<SteamInvite> Game::pending_invite() const {
    auto *game = resolve_game();
    const auto gateway = game ? game->invites() : Ref<GameInvites>();
    return gateway.is_valid() ? gateway->pending() : Ref<SteamInvite>();
}

Ref<GameResult> Game::dismiss_invite(const Ref<SteamInvite> &invite) {
    auto *game = resolve_game();
    if (!game) {
        return reject("GameUnavailable", "Boot a game before dismissing an invitation.");
    }
    const auto gateway = game->invites();
    if (gateway.is_null() || !gateway->is_available()) {
        return reject("InvitesUnavailable", invite_unavailable_message(*game));
    }
    if (!gateway->discard(invite)) {
        return reject("InviteChanged", "This invitation is no longer pending.");
    }
    return GameResult::make(true, "Ok", "The invitation was dismissed.");
}

void Game::observe_invites(GameRuntime &game) {
    if (const auto gateway = game.invites(); gateway.is_valid()) {
        gateway->connect("changed", callable_mp(this, &Game::on_invite_changed));
    }
}

void Game::on_invite_changed(const Ref<SteamInvite> &invite) {
    if (pending_invite() == invite) {
        const Ref<Game> keep_alive(this);
        emit_signal("invite_changed", invite);
    }
}

} // namespace crimson::integration
