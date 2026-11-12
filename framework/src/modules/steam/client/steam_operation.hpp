// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#pragma once

#include "modules/steam/client/steam_result.hpp"

#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/variant/variant.hpp>

#include <functional>

namespace crimson::steam {

// Retained observation of a Steam action with a status-only outcome.
class SteamOperation : public godot::RefCounted {
    GDCLASS(SteamOperation, godot::RefCounted)

protected:
    // Register the retained operation and its typed completion signal.
    static void _bind_methods();

public:
    // Report whether the operation has a terminal result.
    bool is_finished() const;
    // Return the terminal result or null while observation is pending.
    godot::Ref<SteamResult> result() const;
    // Return the settled result or the signal to await.
    godot::Variant wait();
    // Stop observing locally while Steam finishes submitted work.
    godot::Error cancel();

private:
    friend class SteamCloud;
    friend class SteamLeaderboard;
    friend class SteamLeaderboardScope;
    friend class SteamScoreOptions;

    // Continue one native composition immediately on settlement or from the next completion.
    void when_finished(std::function<void(const godot::Ref<SteamResult> &)> next);

    // Settle observation exactly once and emit completion after state is final.
    void finish(const godot::Ref<SteamResult> &outcome);

    // Observer-only cancellation callback with a weak native record reference.
    std::function<godot::Error()> cancel_observation;
    // One internal continuation consumed before the public completion signal.
    std::function<void(const godot::Ref<SteamResult> &)> continuation;
    // Terminal status after the operation finishes.
    godot::Ref<SteamResult> outcome;
};

} // namespace crimson::steam
