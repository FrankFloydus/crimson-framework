// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#include "modules/steam/server/steam_server_progression.hpp"

#include "modules/steam/client/steam_stats.hpp"

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/signal.hpp>

#include <cmath>
#include <cstring>
#include <limits>
#include <utility>

#ifdef CRIMSON_WITH_STEAM
#include "modules/steam/runtime/steam_runtime.hpp"
#include "modules/steam/server/steam_server_runtime.hpp"

#include <godot_cpp/classes/time.hpp>

#include <map>
#endif

namespace crimson::steam {
using namespace godot;

namespace {

// Match Steam's API-name buffer, including space for its terminator.
constexpr int64_t maximum_stat_name_bytes = 127;
// Bound native writes and editor copies within a single submitted draft.
constexpr size_t maximum_edits = 32;
// Expose immutable receipts without a script setter.
constexpr uint32_t read_only = PROPERTY_USAGE_DEFAULT | PROPERTY_USAGE_READ_ONLY;

// Reject names the native null-terminated API could read differently.
bool valid_name(const StringName &name) {
    const CharString text = String(name).utf8();
    return text.length() > 0 && text.length() <= maximum_stat_name_bytes &&
            std::memchr(text.get_data(), 0, static_cast<size_t>(text.length())) == nullptr;
}

// Ensure narrowing to Steam's float format preserves a finite value.
bool valid_float(double value) {
    const double limit = (std::numeric_limits<float>::max)();
    return std::isfinite(value) && value >= -limit && value <= limit;
}

} // namespace

#ifdef CRIMSON_WITH_STEAM

// Track at most one native operation per account, including retired accounts awaiting Steam.
struct SteamServerProgressionState : public std::enable_shared_from_this<SteamServerProgressionState> {
    // Resolve which operation to run once the exact cache is loaded.
    enum class Action { LOAD, EDIT, STORE };

    // Hold room for current players and for native calls that are still draining.
    static constexpr size_t maximum_accounts = 128;
    // End local observation after 15 seconds, without cancelling an already issued store.
    static constexpr uint64_t observation_timeout_ms = 15000;

    // Cache observation and single in-flight operation for this account on this pipe.
    struct Slot {
        // Exact admitted identity to which the loaded cache and edit belong.
        ObjectID identity;
        // Resolve the current admission without retaining the owning Game.
        std::function<ObjectID()> resolve_admission;
        // Whether Steam confirmed a load for this identity since the last unload.
        bool loaded = false;
        // Whether accepted setters may still be in Steam's account cache.
        bool dirty = false;
        // Whether this record is observing a load, an edit, or an explicit store.
        Action action = Action::LOAD;
        // True once the current call is StoreUserStats.
        bool store_submitted = false;
        // Exact native handle. Keep it while draining a cancelled or expired operation.
        SteamAPICall_t call = k_uAPICallInvalid;
        // Scene-clock observation deadline.
        uint64_t deadline = 0;
        // Observer that can be retired independently of native work.
        Ref<SteamServerStatsOperation> observer;
        // Staged edits until a successful load makes preflight possible.
        std::vector<SteamServerStatsEdit::Change> edits;
        // Setters accepted for the current submission.
        int64_t applied = 0;
    };

    // Lifetime of the exact native dedicated server pipe.
    std::weak_ptr<SteamServerRuntime> runtime;
    // Native work and loaded cache state keyed by the Steam account.
    std::map<uint64_t, Slot> slots;
    // Native subscription for eviction of cached stats.
    uint64_t unload_subscription = 0;
    // Mark the state inactive before sending any teardown signals.
    bool closed = false;

    // Resolve the optional dedicated interface and subscribe to cache unload once.
    ISteamGameServerStats *stats_api(const std::shared_ptr<SteamServerRuntime> &sdk) {
        auto *stats = sdk->get_stats();
        if (!stats) {
            return nullptr;
        }
        if (unload_subscription == 0) {
            unload_subscription = sdk->subscribe(GSStatsUnloaded_t::k_iCallback,
                    [weak = weak_from_this()](const void *data, size_t size) {
                        const auto state = weak.lock();
                        if (state && data && size == sizeof(GSStatsUnloaded_t)) {
                            GSStatsUnloaded_t event{};
                            std::memcpy(&event, data, sizeof(event));
                            state->unload(event.m_steamIDUser.ConvertToUint64());
                        }
                    });
        }
        return unload_subscription != 0 ? stats : nullptr;
    }

    // Check the scene-thread runtime and the same exact admission for every public operation.
    Error access(const SteamServerPlayerStats &player, std::shared_ptr<SteamServerRuntime> &sdk,
            ISteamGameServerStats *&stats) {
        if (!SteamRuntime::on_main_thread()) {
            return ERR_BUSY;
        }
        sdk = runtime.lock();
        if (closed || !sdk || !sdk->is_ready()) {
            return ERR_UNAVAILABLE;
        }
        if (!player.resolve_admission || player.resolve_admission() != player.identity) {
            return ERR_UNAUTHORIZED;
        }
        stats = stats_api(sdk);
        return stats ? OK : ERR_UNAVAILABLE;
    }

    // Find the loaded cache only after the exact identity was authenticated.
    bool loaded(const SteamServerPlayerStats &player) const {
        const auto slot = slots.find(player.account);
        return slot != slots.end() && slot->second.identity == player.identity && slot->second.loaded;
    }

    // Reject cache reads until a successful request, or after the SDK unloads it.
    Error read_access(const SteamServerPlayerStats &player, std::shared_ptr<SteamServerRuntime> &sdk,
            ISteamGameServerStats *&stats, const StringName &name) {
        if (!valid_name(name)) {
            return ERR_INVALID_PARAMETER;
        }
        const Error error = access(player, sdk, stats);
        return error == OK && !loaded(player) ? ERR_UNCONFIGURED : error;
    }

    // Start one scoped read or write without reusing native work from another identity.
    Ref<SteamServerStatsOperation> start(const SteamServerPlayerStats &player, Action action,
            const std::vector<SteamServerStatsEdit::Change> &edits = {}) {
        std::shared_ptr<SteamServerRuntime> sdk;
        ISteamGameServerStats *stats = nullptr;
        const Error error = access(player, sdk, stats);
        if (error != OK) {
            return SteamServerStatsOperation::settled(error);
        }
        auto existing = slots.find(player.account);
        if (existing != slots.end() && existing->second.identity != player.identity) {
            if (existing->second.call != k_uAPICallInvalid || existing->second.dirty) {
                return SteamServerStatsOperation::settled(ERR_BUSY);
            }
            slots.erase(existing);
            existing = slots.end();
        }
        if (existing == slots.end()) {
            if (action == Action::STORE) {
                return SteamServerStatsOperation::settled(ERR_UNCONFIGURED);
            }
            if (slots.size() >= maximum_accounts) {
                return SteamServerStatsOperation::settled(ERR_BUSY);
            }
            Slot slot;
            slot.identity = player.identity;
            slot.resolve_admission = player.resolve_admission;
            existing = slots.emplace(player.account, std::move(slot)).first;
        }
        Slot &slot = existing->second;
        if (slot.call != k_uAPICallInvalid || (action == Action::EDIT && slot.dirty)) {
            return SteamServerStatsOperation::settled(ERR_BUSY);
        }
        if (action == Action::LOAD && slot.loaded) {
            return SteamServerStatsOperation::settled(OK);
        }
        if (action == Action::STORE && (!slot.loaded || !slot.dirty)) {
            return SteamServerStatsOperation::settled(slot.loaded ? OK : ERR_UNCONFIGURED);
        }
        if (!sdk->can_track_call()) {
            return SteamServerStatsOperation::settled(ERR_BUSY);
        }

        Ref<SteamServerStatsOperation> observer;
        observer.instantiate();
        slot.action = action;
        slot.store_submitted = false;
        slot.applied = 0;
        slot.edits = edits;
        slot.observer = observer;
        slot.deadline = Time::get_singleton()->get_ticks_msec() + observation_timeout_ms;
        const ObjectID observer_id(observer->get_instance_id());
        observer->cancel_observation = [weak = weak_from_this(), account = player.account, observer_id]() {
            const auto state = weak.lock();
            return state ? state->cancel(account, observer_id) : ERR_DOES_NOT_EXIST;
        };
        if (action == Action::STORE) {
            submit_store(*sdk, *stats, player.account, slot);
        } else if (slot.loaded) {
            apply_edits(*sdk, *stats, player.account, slot);
        } else {
            slot.call = stats->RequestUserStats(CSteamID(player.account));
            if (!watch<GSStatsReceived_t>(*sdk, player.account, slot)) {
                finish(slot, ERR_CANT_CREATE);
            }
        }
        return observer;
    }

    // Match the native call handle, callback type, size and account before consuming a result.
    template <typename Event>
    bool watch(SteamServerRuntime &sdk, uint64_t account, const Slot &slot) {
        return sdk.track_call(slot.call, Event::k_iCallback, sizeof(Event),
                [weak = weak_from_this(), account, call = slot.call](const void *data) {
                    const auto state = weak.lock();
                    if (state) {
                        Event event{};
                        if (data) {
                            std::memcpy(&event, data, sizeof(event));
                        }
                        const bool correct = data && event.m_steamIDUser.ConvertToUint64() == account;
                        state->complete(account, call, correct ? event.m_eResult : k_EResultNone);
                    }
                });
    }

    // Preflight every definition and then apply the authorized edit before asking Steam to store.
    void apply_edits(SteamServerRuntime &sdk, ISteamGameServerStats &stats, uint64_t account, Slot &slot) {
        if (!sdk.can_track_call()) {
            finish(slot, ERR_BUSY);
            return;
        }
        const CSteamID user(account);
        for (size_t index = 0; index < slot.edits.size(); ++index) {
            if (!readable(stats, user, slot.edits[index])) {
                finish(slot, ERR_DOES_NOT_EXIST, static_cast<int64_t>(index));
                return;
            }
        }
        for (size_t index = 0; index < slot.edits.size(); ++index) {
            if (!apply(stats, user, slot.edits[index])) {
                finish(slot, ERR_INVALID_DATA, static_cast<int64_t>(index));
                return;
            }
            ++slot.applied;
            slot.dirty = true;
        }
        submit_store(sdk, stats, account, slot);
    }

    // Check Steam's loaded cache for the named definition and expected read type.
    static bool readable(ISteamGameServerStats &stats, CSteamID user, const SteamServerStatsEdit::Change &change) {
        const CharString name = String(change.name).utf8();
        int32 integer = 0;
        float scalar = 0.0f;
        bool unlocked = false;
        switch (change.kind) {
            case SteamServerStatsEdit::Kind::INTEGER:
                return stats.GetUserStat(user, name.get_data(), &integer);
            case SteamServerStatsEdit::Kind::FLOAT:
            case SteamServerStatsEdit::Kind::AVERAGE_RATE:
                return stats.GetUserStat(user, name.get_data(), &scalar);
            case SteamServerStatsEdit::Kind::UNLOCK:
            case SteamServerStatsEdit::Kind::CLEAR:
                return stats.GetUserAchievement(user, name.get_data(), &unlocked);
        }
        return false;
    }

    // Submit one preflighted cache mutation using Valve's definition-specific setter.
    static bool apply(ISteamGameServerStats &stats, CSteamID user, const SteamServerStatsEdit::Change &change) {
        const CharString name = String(change.name).utf8();
        switch (change.kind) {
            case SteamServerStatsEdit::Kind::INTEGER:
                return stats.SetUserStat(user, name.get_data(), static_cast<int32>(change.integer));
            case SteamServerStatsEdit::Kind::FLOAT:
                return stats.SetUserStat(user, name.get_data(), static_cast<float>(change.scalar));
            case SteamServerStatsEdit::Kind::AVERAGE_RATE:
                return stats.UpdateUserAvgRateStat(user, name.get_data(), static_cast<float>(change.scalar), change.seconds);
            case SteamServerStatsEdit::Kind::UNLOCK:
                return stats.SetUserAchievement(user, name.get_data());
            case SteamServerStatsEdit::Kind::CLEAR:
                return stats.ClearUserAchievement(user, name.get_data());
        }
        return false;
    }

    // Issue one store. Mark submission before observation so local errors cannot claim rollback.
    void submit_store(SteamServerRuntime &sdk, ISteamGameServerStats &stats, uint64_t account, Slot &slot) {
        slot.edits.clear();
        slot.call = stats.StoreUserStats(CSteamID(account));
        slot.store_submitted = slot.call != k_uAPICallInvalid;
        if (!watch<GSStatsStored_t>(sdk, account, slot)) {
            finish(slot, ERR_CANT_CREATE);
        }
    }

    // Consume the tracked result once. A retired admission cannot issue setters from a late load.
    void complete(uint64_t account, SteamAPICall_t call, EResult result) {
        const auto found = slots.find(account);
        if (closed || found == slots.end() || found->second.call != call) {
            return;
        }
        Slot &slot = found->second;
        slot.call = k_uAPICallInvalid;
        const bool admission_is_current = slot.resolve_admission() == slot.identity;
        const bool expired = Time::get_singleton()->get_ticks_msec() >= slot.deadline;
        if (slot.store_submitted) {
            if (!slot.loaded || result == k_EResultOK || result == k_EResultInvalidParam) {
                slot.dirty = false;
            }
            if (result == k_EResultInvalidParam) {
                slot.loaded = false;
            }
            const Error error = !admission_is_current ? ERR_SKIP : (expired ? ERR_TIMEOUT : outcome_error(result));
            finish(slot, error, -1, expired ? k_EResultNone : result);
            return;
        }
        if (!admission_is_current || slot.observer.is_null()) {
            finish(slot, ERR_SKIP);
            return;
        }
        if (expired) {
            finish(slot, ERR_TIMEOUT);
            return;
        }
        if (result != k_EResultOK) {
            finish(slot, outcome_error(result), -1, result);
            return;
        }
        slot.loaded = true;
        if (slot.action == Action::LOAD) {
            finish(slot, OK, -1, result);
            return;
        }
        const auto sdk = runtime.lock();
        auto *stats = sdk && sdk->is_ready() ? stats_api(sdk) : nullptr;
        if (!stats) {
            finish(slot, ERR_UNAVAILABLE);
            return;
        }
        apply_edits(*sdk, *stats, account, slot);
    }

    // Translate the public error while retaining Steam's native result on the receipt.
    static Error outcome_error(EResult result) {
        switch (result) {
            case k_EResultOK: return OK;
            case k_EResultInvalidParam: return ERR_INVALID_DATA;
            case k_EResultAccessDenied:
            case k_EResultInsufficientPrivilege: return ERR_UNAUTHORIZED;
            case k_EResultTimeout: return ERR_TIMEOUT;
            case k_EResultBusy:
            case k_EResultRateLimitExceeded: return ERR_BUSY;
            case k_EResultNone: return ERR_CANT_CONNECT;
            default: return FAILED;
        }
    }

    // Clear staged work before handing the terminal outcome to potentially reentrant script.
    static void finish(Slot &slot, Error error, int64_t failed_at = -1, int64_t steam_result = 0) {
        const auto observer = std::exchange(slot.observer, {});
        slot.edits.clear();
        if (observer.is_valid()) {
            observer->finish(SteamServerStatsResult::create(error, slot.applied, failed_at, steam_result,
                    slot.store_submitted));
        }
    }

    // Invalidate cache state on Valve eviction without claiming an issued store was cancelled.
    void unload(uint64_t account) {
        const auto found = slots.find(account);
        if (found != slots.end()) {
            Slot &slot = found->second;
            slot.loaded = false;
            if (!slot.store_submitted || slot.call == k_uAPICallInvalid) {
                slot.dirty = false;
            }
        }
    }

    // Cancel only the observer that owns this operation.
    Error cancel(uint64_t account, ObjectID observer_id) {
        const auto found = slots.find(account);
        if (found == slots.end() || found->second.observer.is_null() ||
                ObjectID(found->second.observer->get_instance_id()) != observer_id) {
            return ERR_DOES_NOT_EXIST;
        }
        finish(found->second, ERR_SKIP);
        return OK;
    }

    // Collect and then publish expirations so completion handlers can safely change the Game.
    void tick() {
        const uint64_t now = Time::get_singleton()->get_ticks_msec();
        std::vector<std::pair<Ref<SteamServerStatsOperation>, Ref<SteamServerStatsResult>>> completed;
        for (auto slot = slots.begin(); slot != slots.end();) {
            auto &record = slot->second;
            const bool admitted = record.resolve_admission() == record.identity;
            if (record.observer.is_valid() && (!admitted || now >= record.deadline)) {
                completed.emplace_back(std::exchange(record.observer, {}),
                        SteamServerStatsResult::create(admitted ? ERR_TIMEOUT : ERR_SKIP,
                                record.applied, -1, 0, record.store_submitted));
                record.edits.clear();
            }
            if (!admitted && record.call == k_uAPICallInvalid && !record.dirty) {
                slot = slots.erase(slot);
            } else {
                ++slot;
            }
        }
        for (const auto &[observer, result] : completed) {
            observer->finish(result);
        }
    }

    // Release the pipe's exact watchers before notifying that this runtime retired.
    void close() {
        closed = true;
        auto retired = std::move(slots);
        if (const auto sdk = runtime.lock()) {
            sdk->unsubscribe(unload_subscription);
            for (const auto &entry : retired) {
                sdk->forget_call(entry.second.call);
            }
        }
        runtime.reset();
        for (auto &entry : retired) {
            finish(entry.second, ERR_SKIP);
        }
    }
};

#endif

void SteamServerStatsResult::_bind_methods() {
    ClassDB::bind_method(D_METHOD("get_applied"), &SteamServerStatsResult::get_applied);
    ClassDB::bind_method(D_METHOD("get_failed_at"), &SteamServerStatsResult::get_failed_at);
    ClassDB::bind_method(D_METHOD("get_steam_result"), &SteamServerStatsResult::get_steam_result);
    ClassDB::bind_method(D_METHOD("was_store_submitted"), &SteamServerStatsResult::was_store_submitted);
    ADD_PROPERTY(PropertyInfo(Variant::INT, "applied", PROPERTY_HINT_NONE, "", read_only), "", "get_applied");
    ADD_PROPERTY(PropertyInfo(Variant::INT, "failed_at", PROPERTY_HINT_NONE, "", read_only), "", "get_failed_at");
    ADD_PROPERTY(PropertyInfo(Variant::INT, "steam_result", PROPERTY_HINT_NONE, "", read_only), "", "get_steam_result");
    ADD_PROPERTY(PropertyInfo(Variant::BOOL, "store_submitted", PROPERTY_HINT_NONE, "", read_only), "", "was_store_submitted");
}

int64_t SteamServerStatsResult::get_applied() const { return applied; }
int64_t SteamServerStatsResult::get_failed_at() const { return failed_at; }
int64_t SteamServerStatsResult::get_steam_result() const { return steam_result; }
bool SteamServerStatsResult::was_store_submitted() const { return store_submitted; }

Ref<SteamServerStatsResult> SteamServerStatsResult::create(Error error, int64_t applied, int64_t failed_at,
        int64_t steam_result, bool store_submitted) {
    Ref<SteamServerStatsResult> result;
    result.instantiate();
    result->set_outcome(error);
    result->applied = applied;
    result->failed_at = failed_at;
    result->steam_result = steam_result;
    result->store_submitted = store_submitted;
    return result;
}

void SteamServerStatsOperation::_bind_methods() {
    ClassDB::bind_method(D_METHOD("is_finished"), &SteamServerStatsOperation::is_finished);
    ClassDB::bind_method(D_METHOD("result"), &SteamServerStatsOperation::result);
    ClassDB::bind_method(D_METHOD("wait"), &SteamServerStatsOperation::wait);
    ClassDB::bind_method(D_METHOD("cancel"), &SteamServerStatsOperation::cancel);
    ADD_SIGNAL(MethodInfo("completed", PropertyInfo(Variant::OBJECT, "result", PROPERTY_HINT_RESOURCE_TYPE,
            "SteamServerStatsResult")));
}

bool SteamServerStatsOperation::is_finished() const { return outcome.is_valid(); }
Ref<SteamServerStatsResult> SteamServerStatsOperation::result() const { return outcome; }
Variant SteamServerStatsOperation::wait() { return outcome.is_valid() ? Variant(outcome) : Variant(Signal(this, "completed")); }

Error SteamServerStatsOperation::cancel() {
#ifdef CRIMSON_WITH_STEAM
    if (!SteamRuntime::on_main_thread()) {
        return ERR_BUSY;
    }
#endif
    const auto cancel = cancel_observation;
    return cancel ? cancel() : ERR_DOES_NOT_EXIST;
}

Ref<SteamServerStatsOperation> SteamServerStatsOperation::settled(Error error, int64_t failed_at) {
    Ref<SteamServerStatsOperation> operation;
    operation.instantiate();
    operation->finish(SteamServerStatsResult::create(error, 0, failed_at, 0, false));
    return operation;
}

void SteamServerStatsOperation::finish(const Ref<SteamServerStatsResult> &result) {
    if (outcome.is_valid()) {
        return;
    }
    const Ref<SteamServerStatsOperation> keep_alive(this);
    outcome = result;
    cancel_observation = {};
    emit_signal("completed", outcome);
}

void SteamServerPlayerStats::_bind_methods() {
    ClassDB::bind_method(D_METHOD("is_available"), &SteamServerPlayerStats::is_available);
    ClassDB::bind_method(D_METHOD("is_loaded"), &SteamServerPlayerStats::is_loaded);
    ClassDB::bind_method(D_METHOD("load"), &SteamServerPlayerStats::load);
    ClassDB::bind_method(D_METHOD("stat_int", "name"), &SteamServerPlayerStats::stat_int);
    ClassDB::bind_method(D_METHOD("stat_float", "name"), &SteamServerPlayerStats::stat_float);
    ClassDB::bind_method(D_METHOD("achievement", "name"), &SteamServerPlayerStats::achievement);
    ClassDB::bind_method(D_METHOD("edit"), &SteamServerPlayerStats::edit);
    ClassDB::bind_method(D_METHOD("store"), &SteamServerPlayerStats::store);
}

bool SteamServerPlayerStats::is_available() const {
#ifdef CRIMSON_WITH_STEAM
    const auto owner = state.lock();
    std::shared_ptr<SteamServerRuntime> sdk;
    ISteamGameServerStats *stats = nullptr;
    return owner && owner->access(*this, sdk, stats) == OK;
#else
    return false;
#endif
}

bool SteamServerPlayerStats::is_loaded() const {
#ifdef CRIMSON_WITH_STEAM
    const auto owner = state.lock();
    return is_available() && owner && owner->loaded(*this);
#else
    return false;
#endif
}

Ref<SteamServerStatsOperation> SteamServerPlayerStats::load() const {
#ifdef CRIMSON_WITH_STEAM
    if (const auto owner = state.lock()) {
        return owner->start(*this, SteamServerProgressionState::Action::LOAD);
    }
#endif
    return SteamServerStatsOperation::settled(ERR_UNAVAILABLE);
}

Ref<SteamIntResult> SteamServerPlayerStats::stat_int(const StringName &name) const {
#ifdef CRIMSON_WITH_STEAM
    if (const auto owner = state.lock()) {
        std::shared_ptr<SteamServerRuntime> sdk;
        ISteamGameServerStats *stats = nullptr;
        int32 value = 0;
        Error error = owner->read_access(*this, sdk, stats, name);
        if (error == OK && !stats->GetUserStat(CSteamID(account), String(name).utf8().get_data(), &value)) {
            error = ERR_DOES_NOT_EXIST;
        }
        return SteamIntResult::create(error, value);
    }
#endif
    return SteamIntResult::create(ERR_UNAVAILABLE, 0);
}

Ref<SteamFloatResult> SteamServerPlayerStats::stat_float(const StringName &name) const {
#ifdef CRIMSON_WITH_STEAM
    if (const auto owner = state.lock()) {
        std::shared_ptr<SteamServerRuntime> sdk;
        ISteamGameServerStats *stats = nullptr;
        float value = 0.0f;
        Error error = owner->read_access(*this, sdk, stats, name);
        if (error == OK && !stats->GetUserStat(CSteamID(account), String(name).utf8().get_data(), &value)) {
            error = ERR_DOES_NOT_EXIST;
        } else if (error == OK && !std::isfinite(value)) {
            error = ERR_INVALID_DATA;
        }
        return SteamFloatResult::create(error, value);
    }
#endif
    return SteamFloatResult::create(ERR_UNAVAILABLE, 0.0);
}

Ref<SteamBoolResult> SteamServerPlayerStats::achievement(const StringName &name) const {
#ifdef CRIMSON_WITH_STEAM
    if (const auto owner = state.lock()) {
        std::shared_ptr<SteamServerRuntime> sdk;
        ISteamGameServerStats *stats = nullptr;
        bool unlocked = false;
        Error error = owner->read_access(*this, sdk, stats, name);
        if (error == OK && !stats->GetUserAchievement(CSteamID(account), String(name).utf8().get_data(), &unlocked)) {
            error = ERR_DOES_NOT_EXIST;
        }
        return SteamBoolResult::create(error, unlocked);
    }
#endif
    return SteamBoolResult::create(ERR_UNAVAILABLE);
}

Ref<SteamServerStatsEdit> SteamServerPlayerStats::edit() {
    Ref<SteamServerStatsEdit> draft;
    draft.instantiate();
    draft->player = Ref<SteamServerPlayerStats>(this);
    return draft;
}

Ref<SteamServerStatsOperation> SteamServerPlayerStats::store() const {
#ifdef CRIMSON_WITH_STEAM
    if (const auto owner = state.lock()) {
        return owner->start(*this, SteamServerProgressionState::Action::STORE);
    }
#endif
    return SteamServerStatsOperation::settled(ERR_UNAVAILABLE);
}

void SteamServerStatsEdit::_bind_methods() {
    ClassDB::bind_method(D_METHOD("set_int", "name", "value"), &SteamServerStatsEdit::set_int);
    ClassDB::bind_method(D_METHOD("set_float", "name", "value"), &SteamServerStatsEdit::set_float);
    ClassDB::bind_method(D_METHOD("update_average_rate", "name", "count", "seconds"),
            &SteamServerStatsEdit::update_average_rate);
    ClassDB::bind_method(D_METHOD("unlock", "name"), &SteamServerStatsEdit::unlock);
    ClassDB::bind_method(D_METHOD("clear_achievement", "name"), &SteamServerStatsEdit::clear_achievement);
    ClassDB::bind_method(D_METHOD("submit"), &SteamServerStatsEdit::submit);
}

Ref<SteamServerStatsEdit> SteamServerStatsEdit::set_int(const StringName &name, int64_t value) const {
    return append(Change{Kind::INTEGER, name, value}, value >= (std::numeric_limits<int32_t>::min)() &&
            value <= (std::numeric_limits<int32_t>::max)());
}

Ref<SteamServerStatsEdit> SteamServerStatsEdit::set_float(const StringName &name, double value) const {
    return append(Change{Kind::FLOAT, name, 0, value}, valid_float(value));
}

Ref<SteamServerStatsEdit> SteamServerStatsEdit::update_average_rate(const StringName &name, double count, double seconds) const {
    return append(Change{Kind::AVERAGE_RATE, name, 0, count, seconds},
            valid_float(count) && std::isfinite(seconds) && seconds > 0.0);
}

Ref<SteamServerStatsEdit> SteamServerStatsEdit::unlock(const StringName &name) const {
    return append(Change{Kind::UNLOCK, name}, true);
}

Ref<SteamServerStatsEdit> SteamServerStatsEdit::clear_achievement(const StringName &name) const {
    return append(Change{Kind::CLEAR, name}, true);
}

Ref<SteamServerStatsEdit> SteamServerStatsEdit::append(Change change, bool valid_value) const {
    Ref<SteamServerStatsEdit> draft;
    draft.instantiate();
    draft->player = player;
    draft->changes = changes;
    draft->invalid_edit = invalid_edit;
    if (draft->invalid_edit == -1) {
        if (changes.size() >= maximum_edits || !valid_value || !valid_name(change.name)) {
            draft->invalid_edit = static_cast<int64_t>(changes.size());
        } else {
            draft->changes.push_back(std::move(change));
        }
    }
    return draft;
}

Ref<SteamServerStatsOperation> SteamServerStatsEdit::submit() {
    if (submission.is_valid()) {
        return submission;
    }
    if (invalid_edit >= 0 || changes.empty()) {
        submission = SteamServerStatsOperation::settled(ERR_INVALID_PARAMETER, invalid_edit);
        return submission;
    }
#ifdef CRIMSON_WITH_STEAM
    if (player.is_valid()) {
        if (const auto owner = player->state.lock()) {
            submission = owner->start(*player.ptr(), SteamServerProgressionState::Action::EDIT, changes);
            return submission;
        }
    }
#endif
    submission = SteamServerStatsOperation::settled(ERR_UNAVAILABLE);
    return submission;
}

SteamServerProgression::SteamServerProgression(const std::shared_ptr<SteamServerRuntime> &runtime) {
#ifdef CRIMSON_WITH_STEAM
    state = std::make_shared<SteamServerProgressionState>();
    state->runtime = runtime;
#endif
}

SteamServerProgression::~SteamServerProgression() {
#ifdef CRIMSON_WITH_STEAM
    state->close();
#endif
}

Ref<SteamServerPlayerStats> SteamServerProgression::for_peer(uint64_t account, ObjectID identity,
        std::function<ObjectID()> resolve_admission) const {
#ifdef CRIMSON_WITH_STEAM
    if (!SteamRuntime::on_main_thread() || account == 0 || !identity.is_valid() ||
            !resolve_admission || resolve_admission() != identity) {
        return {};
    }
    Ref<SteamServerPlayerStats> result;
    result.instantiate();
    result->state = state;
    result->account = account;
    result->identity = identity;
    result->resolve_admission = std::move(resolve_admission);
    return result;
#else
    return {};
#endif
}

void SteamServerProgression::tick() {
#ifdef CRIMSON_WITH_STEAM
    const auto owner = state;
    owner->tick();
#endif
}

} // namespace crimson::steam
