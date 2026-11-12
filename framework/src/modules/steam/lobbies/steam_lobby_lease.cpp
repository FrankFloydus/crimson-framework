// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#include "modules/steam/lobbies/steam_lobby_lease.hpp"

#ifdef CRIMSON_WITH_STEAM

#include "modules/steam/runtime/steam_runtime.hpp"

#include <utility>

namespace crimson::steam {
namespace {

// Keep an abandoned pending SDK request exclusive until its completion is drained.
std::shared_ptr<SteamLobbyLease> account_lease;

} // namespace

std::shared_ptr<SteamLobbyLease> SteamLobbyLease::reserve(const std::shared_ptr<SteamRuntime> &sdk, bool host) {
    if (!sdk || !sdk->is_healthy() || !sdk->get_matchmaking() || (account_lease && !account_lease->runtime.expired())) {
        return {};
    }
    auto lease = std::make_shared<SteamLobbyLease>();
    lease->runtime = sdk;
    lease->hosting = host;
    account_lease = lease;
    return lease;
}

void SteamLobbyLease::forget_runtime(const SteamRuntime &runtime) {
    if (account_lease && account_lease->runtime.lock().get() == &runtime) {
        account_lease->retired = true;
        account_lease->pending = false;
        account_lease->lobby = 0;
        account_lease.reset();
    }
}

bool SteamLobbyLease::settle(uint64_t acquired_lobby) {
    if (account_lease.get() != this || !std::exchange(pending, false)) {
        return false;
    }
    lobby = acquired_lobby;
    if (retired || lobby == 0) {
        retire();
        return false;
    }
    return true;
}

void SteamLobbyLease::retire() {
    retired = true;
    if (pending || account_lease.get() != this) {
        return;
    }
    const uint64_t retired_lobby = std::exchange(lobby, 0);
    const auto sdk = runtime.lock();
    if (sdk && retired_lobby != 0) {
        auto *api = sdk->get_matchmaking();
        const CSteamID id(retired_lobby);
        if (hosting && api->GetLobbyOwner(id).ConvertToUint64() == sdk->get_user_id()) {
            api->SetLobbyJoinable(id, false);
        }
        api->LeaveLobby(id);
    }
    account_lease.reset();
}

uint64_t SteamLobbyLease::current() const {
    return retired || account_lease.get() != this ? 0 : lobby;
}

} // namespace crimson::steam

#endif
