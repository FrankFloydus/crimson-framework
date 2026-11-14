// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#pragma once

#include "integration/game/game_services.hpp"
#include "modules/steam/client/steamworks.hpp"

namespace crimson::integration {

// Resolve the configured Steam provider without attaching any SDK scope.
godot::Ref<GameServiceProvider> make_steam_provider(const godot::Ref<steam::SteamConfig> &settings);

// Expose Steam capabilities for the already attached game, including startup errors.
godot::Ref<steam::Steamworks> steamworks_for_game(GameRuntime *game);

} // namespace crimson::integration
