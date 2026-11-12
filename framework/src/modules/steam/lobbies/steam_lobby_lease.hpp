// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#pragma once

#ifdef CRIMSON_WITH_STEAM

#include <cstdint>
#include <memory>

namespace crimson::steam {

class SteamRuntime;

// The process Steam account owns one exact pending or committed lobby membership.
class SteamLobbyLease {
public:
    // Reserve the account before a native create or join can have side effects.
    static std::shared_ptr<SteamLobbyLease> reserve(const std::shared_ptr<SteamRuntime> &runtime, bool hosting);
    // Drop the account reservation after its client pipe loses all pending completions.
    static void forget_runtime(const SteamRuntime &runtime);
    // Accept a successful native membership, or compensate a retired attempt before releasing the reservation.
    bool settle(uint64_t lobby);
    // Leave a settled membership or retain the reservation until a pending call can be drained.
    void retire();
    // Return this lease's committed lobby, or zero after retirement.
    uint64_t current() const;

private:
    // The reserved process client without extending its lifetime.
    std::weak_ptr<SteamRuntime> runtime;
    // Exact SDK lobby ID after successful native completion.
    uint64_t lobby = 0;
    // Hosting attempts close joinability before leaving if the account is still the owner.
    bool hosting = false;
    // An outstanding native call can acquire membership even after caller retirement.
    bool pending = true;
    // Prevent future use of this exact lease.
    bool retired = false;
};

} // namespace crimson::steam

#endif
