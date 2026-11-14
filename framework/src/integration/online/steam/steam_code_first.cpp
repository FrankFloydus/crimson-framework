// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#include "integration/online/steam/steam_code_first.hpp"

#include "integration/online/steam/steam_online.hpp"

namespace crimson::integration {
using namespace godot;

Ref<GameServiceProvider> make_steam_provider(const Ref<steam::SteamConfig> &settings) {
    auto provider = SteamProvider::configure();
    if (settings.is_null()) {
        return provider;
    }
    if (!settings->is_valid()) {
        return {};
    }
    provider = provider->for_app(settings->get_app_id());
    if (!settings->get_test_group().is_empty()) {
        provider = provider->with_lobby_namespace(settings->get_test_group());
    }
    if (settings->is_public()) {
        provider = provider->public_lobbies();
    }
    return provider;
}

Ref<steam::Steamworks> steamworks_for_game(GameRuntime *game) {
    const auto scope = SteamServices::for_game(game);
    if (scope.is_null()) {
        return steam::Steamworks::unavailable(ERR_UNAVAILABLE, "This game has no attached Steam client scope.");
    }
    if (auto *client = scope->resolve_client()) {
        return steam::Steamworks::from_client(client);
    }
    const String reason = scope->get_initialization_error();
    return steam::Steamworks::unavailable(ERR_UNAVAILABLE,
            reason.is_empty() ? "The game's Steam client scope is no longer available." : reason);
}

} // namespace crimson::integration
