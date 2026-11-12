// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#include "modules/steam/client/steam_stats.hpp"
#include "modules/steam/client/steam_client_scope.hpp"

#include <godot_cpp/core/class_db.hpp>

#include <utility>

#ifdef CRIMSON_WITH_STEAM
#include "modules/steam/runtime/steam_runtime.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <iterator>
#include <limits>
#endif

namespace crimson::steam {
using namespace godot;

namespace {

// Snapshot properties are read-only and are never saved as instance state.
constexpr uint32_t read_only = PROPERTY_USAGE_NO_INSTANCE_STATE | PROPERTY_USAGE_READ_ONLY;

// Describe why a read failed without exposing the player's cached stat value.
String stat_read_message(Error error) {
    switch (error) {
        case ERR_INVALID_PARAMETER:
            return "Provide a valid published Steam stat name.";
        case ERR_DOES_NOT_EXIST:
            return "Steam has no readable stat of the requested type for this name.";
        case ERR_UNAVAILABLE:
            return "Steam stats are unavailable in this client scope.";
        default:
            return {};
    }
}

#ifdef CRIMSON_WITH_STEAM

// Resolve User Stats lazily so this optional interface never gates client startup.
ISteamUserStats *find_user_stats(const std::shared_ptr<SteamRuntime> &active) {
    if (!active) {
        return nullptr;
    }
    const auto find = active->symbol<decltype(&SteamAPI_SteamUserStats_v013)>("SteamAPI_SteamUserStats_v013");
    return find ? find() : nullptr;
}

// Accept a nonempty API name within the Steam UTF-8 buffer.
bool valid_name(const CharString &name) {
    const int64_t length = name.length();
    return length > 0 && length < k_cchStatNameMax &&
            std::memchr(name.get_data(), 0, static_cast<size_t>(length)) == nullptr;
}

// Read a typed value without treating a missing or mismatched stat as zero.
template <typename Value, typename Read>
Error read_stat(const std::shared_ptr<SteamRuntime> &active, const CharString &name, const char *symbol, Value &value) {
    ISteamUserStats *stats = find_user_stats(active);
    if (!stats) {
        return ERR_UNAVAILABLE;
    }
    if (!valid_name(name)) {
        return ERR_INVALID_PARAMETER;
    }
    const auto read = active->symbol<Read>(symbol);
    if (!read) {
        return ERR_UNAVAILABLE;
    }
    return read(stats, name.get_data(), &value) ? OK : ERR_DOES_NOT_EXIST;
}

// Read a signed integer from the signed-in player's local Steam stats cache.
Error read_int(const std::shared_ptr<SteamRuntime> &active, const CharString &name, int32 &value) {
    return read_stat<int32, decltype(&SteamAPI_ISteamUserStats_GetStatInt32)>(active, name,
            "SteamAPI_ISteamUserStats_GetStatInt32", value);
}

// Read a float or the current value of an average-rate stat from the cache.
Error read_float(const std::shared_ptr<SteamRuntime> &active, const CharString &name, float &value) {
    return read_stat<float, decltype(&SteamAPI_ISteamUserStats_GetStatFloat)>(active, name,
            "SteamAPI_ISteamUserStats_GetStatFloat", value);
}

// Submit a validated named cache update and report SDK rejection.
template <typename Write, typename... Args>
Error write_stat(const std::weak_ptr<SteamRuntime> &runtime, const String &name, const char *symbol, Args... args) {
    if (!SteamRuntime::on_main_thread()) {
        return ERR_BUSY;
    }
    const CharString encoded = name.utf8();
    if (!valid_name(encoded)) {
        return ERR_INVALID_PARAMETER;
    }
    const auto active = runtime.lock();
    ISteamUserStats *stats = find_user_stats(active);
    if (!stats) {
        return ERR_UNAVAILABLE;
    }
    const auto write = active->symbol<Write>(symbol);
    if (!write) {
        return ERR_UNAVAILABLE;
    }
    return write(stats, encoded.get_data(), args...) ? OK : FAILED;
}

// Check the numeric limits before narrowing a Godot double to an SDK float.
bool finite_float(double value) {
    return std::isfinite(value) && std::abs(value) <= (std::numeric_limits<float>::max)();
}

// Check the numeric limits before narrowing a Godot integer to a Steam integer stat.
bool valid_int(int64_t value) {
    return value >= (std::numeric_limits<int32>::min)() && value <= (std::numeric_limits<int32>::max)();
}

// Check an average-rate change before narrowing its count and submitting its duration.
bool valid_average_rate(double count, double seconds) {
    return finite_float(count) && std::isfinite(seconds) && seconds > 0.0;
}

// Holds the Steam interface and persistence function resolved for this call.
struct StatsStore {
    // Resolution status before a call can submit any cache changes.
    Error error;
    // Borrowed SDK interface, valid while the caller holds the runtime lease.
    ISteamUserStats *stats = nullptr;
    // Function that requests persistence for the app-wide cache.
    decltype(&SteamAPI_ISteamUserStats_StoreStats) request = nullptr;
};

// Resolve persistence and its callback before applying any draft changes.
StatsStore find_store(const std::shared_ptr<SteamRuntime> &active, uint64_t stored_subscription) {
    ISteamUserStats *stats = find_user_stats(active);
    if (!stats) {
        return {ERR_UNAVAILABLE};
    }
    if (!stored_subscription) {
        return {ERR_BUSY};
    }
    const auto request = active->symbol<decltype(&SteamAPI_ISteamUserStats_StoreStats)>("SteamAPI_ISteamUserStats_StoreStats");
    return request ? StatsStore{OK, stats, request} : StatsStore{ERR_UNAVAILABLE};
}

#endif
} // namespace

#ifdef CRIMSON_WITH_STEAM
// Reads achievement state and display attributes while the caller holds the SDK runtime lease.
struct SteamStats::AchievementReader {
    // Resolve the native readers once for the whole lookup or list.
    explicit AchievementReader(const std::shared_ptr<SteamRuntime> &active) :
            stats(find_user_stats(active)) {
        if (stats) {
            read = active->symbol<decltype(read)>("SteamAPI_ISteamUserStats_GetAchievementAndUnlockTime");
            attribute = active->symbol<decltype(attribute)>("SteamAPI_ISteamUserStats_GetAchievementDisplayAttribute");
        }
    }

    // Read cached unlock state without building a snapshot or fetching display attributes.
    Error state(const CharString &name, bool &unlocked, uint32 &time) const {
        if (!read) {
            return ERR_UNAVAILABLE;
        }
        if (!valid_name(name)) {
            return ERR_INVALID_PARAMETER;
        }
        return read(stats, name.get_data(), &unlocked, &time) ? OK : ERR_DOES_NOT_EXIST;
    }

    // Copy a localized string immediately because Steam owns its memory.
    String display_attribute(const CharString &name, const char *key) const {
        const char *value = attribute ? attribute(stats, name.get_data(), key) : nullptr;
        return value ? String::utf8(value) : String();
    }

    // Return a detached object for readable cached state, including when display attributes are omitted.
    Ref<SteamAchievement> snapshot(const StringName &api_name) const {
        const String identifier(api_name);
        const CharString name = identifier.utf8();
        bool unlocked = false;
        uint32 time = 0;
        if (state(name, unlocked, time) != OK) {
            return {};
        }
        Ref<SteamAchievement> achievement;
        achievement.instantiate();
        achievement->api_name = api_name;
        const String localized_name = display_attribute(name, "name");
        achievement->display_name = localized_name.is_empty() ? identifier : localized_name;
        achievement->description = display_attribute(name, "desc");
        achievement->hidden = display_attribute(name, "hidden") == "1";
        achievement->unlocked = unlocked;
        achievement->unlocked_at_unix = unlocked ? static_cast<int64_t>(time) : 0;
        return achievement;
    }

    // Borrowed User Stats interface valid while the caller holds its SDK lease.
    ISteamUserStats *stats = nullptr;
    // Read the cache's unlocked state and unlock timestamp.
    decltype(&SteamAPI_ISteamUserStats_GetAchievementAndUnlockTime) read = nullptr;
    // Read a localized display or hidden attribute as a borrowed UTF-8 string.
    decltype(&SteamAPI_ISteamUserStats_GetAchievementDisplayAttribute) attribute = nullptr;
};
#endif

void SteamIntResult::_bind_methods() {
    ClassDB::bind_method(D_METHOD("get_value"), &SteamIntResult::get_value);
    ADD_PROPERTY(PropertyInfo(Variant::INT, "value", PROPERTY_HINT_NONE, "", read_only), "", "get_value");
}

int64_t SteamIntResult::get_value() const {
    return value;
}

Ref<SteamIntResult> SteamIntResult::create(Error error, int64_t value) {
    Ref<SteamIntResult> result;
    result.instantiate();
    result->set_outcome(error, stat_read_message(error));
    if (error == OK) {
        result->value = value;
    }
    return result;
}

void SteamFloatResult::_bind_methods() {
    ClassDB::bind_method(D_METHOD("get_value"), &SteamFloatResult::get_value);
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "value", PROPERTY_HINT_NONE, "", read_only), "", "get_value");
}

double SteamFloatResult::get_value() const {
    return value;
}

Ref<SteamFloatResult> SteamFloatResult::create(Error error, double value) {
    Ref<SteamFloatResult> result;
    result.instantiate();
    result->set_outcome(error, stat_read_message(error));
    if (error == OK) {
        result->value = value;
    }
    return result;
}

void SteamAchievement::_bind_methods() {
    ClassDB::bind_method(D_METHOD("get_api_name"), &SteamAchievement::get_api_name);
    ClassDB::bind_method(D_METHOD("get_display_name"), &SteamAchievement::get_display_name);
    ClassDB::bind_method(D_METHOD("get_description"), &SteamAchievement::get_description);
    ClassDB::bind_method(D_METHOD("is_hidden"), &SteamAchievement::is_hidden);
    ClassDB::bind_method(D_METHOD("is_unlocked"), &SteamAchievement::is_unlocked);
    ClassDB::bind_method(D_METHOD("get_unlocked_at_unix"), &SteamAchievement::get_unlocked_at_unix);
    ADD_PROPERTY(PropertyInfo(Variant::STRING_NAME, "api_name", PROPERTY_HINT_NONE, "", read_only), "", "get_api_name");
    ADD_PROPERTY(PropertyInfo(Variant::STRING, "display_name", PROPERTY_HINT_NONE, "", read_only), "", "get_display_name");
    ADD_PROPERTY(PropertyInfo(Variant::STRING, "description", PROPERTY_HINT_NONE, "", read_only), "", "get_description");
    ADD_PROPERTY(PropertyInfo(Variant::BOOL, "hidden", PROPERTY_HINT_NONE, "", read_only), "", "is_hidden");
    ADD_PROPERTY(PropertyInfo(Variant::BOOL, "unlocked", PROPERTY_HINT_NONE, "", read_only), "", "is_unlocked");
    ADD_PROPERTY(PropertyInfo(Variant::INT, "unlocked_at_unix", PROPERTY_HINT_NONE, "", read_only), "", "get_unlocked_at_unix");
}

StringName SteamAchievement::get_api_name() const {
    return api_name;
}

String SteamAchievement::get_display_name() const {
    return display_name;
}

String SteamAchievement::get_description() const {
    return description;
}

bool SteamAchievement::is_hidden() const {
    return hidden;
}

bool SteamAchievement::is_unlocked() const {
    return unlocked;
}

int64_t SteamAchievement::get_unlocked_at_unix() const {
    return unlocked_at_unix;
}

void SteamStatsSubmission::_bind_methods() {
    ClassDB::bind_method(D_METHOD("get_applied"), &SteamStatsSubmission::get_applied);
    ClassDB::bind_method(D_METHOD("get_failed_at"), &SteamStatsSubmission::get_failed_at);
    ADD_PROPERTY(PropertyInfo(Variant::INT, "applied", PROPERTY_HINT_NONE, "", read_only), "", "get_applied");
    ADD_PROPERTY(PropertyInfo(Variant::INT, "failed_at", PROPERTY_HINT_NONE, "", read_only), "", "get_failed_at");
}

int64_t SteamStatsSubmission::get_applied() const {
    return applied;
}

int64_t SteamStatsSubmission::get_failed_at() const {
    return failed_at;
}

void SteamStatsSubmissionResult::_bind_methods() {
    ClassDB::bind_method(D_METHOD("get_value"), &SteamStatsSubmissionResult::get_value);
    ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "value", PROPERTY_HINT_RESOURCE_TYPE,
                         SteamStatsSubmission::get_class_static(), read_only), "", "get_value");
}

Ref<SteamStatsSubmission> SteamStatsSubmissionResult::get_value() const {
    return value;
}

Ref<SteamStatsSubmissionResult> SteamStatsSubmissionResult::create(Error error, int64_t applied, int64_t failed_at,
        const String &message) {
    Ref<SteamStatsSubmissionResult> result;
    result.instantiate();
    result->set_outcome(error, message);
    result->value.instantiate();
    result->value->applied = applied;
    result->value->failed_at = failed_at;
    return result;
}

void SteamStatsEdit::_bind_methods() {
    ClassDB::bind_method(D_METHOD("set_int", "name", "value"), &SteamStatsEdit::set_int);
    ClassDB::bind_method(D_METHOD("set_float", "name", "value"), &SteamStatsEdit::set_float);
    ClassDB::bind_method(D_METHOD("update_average_rate", "name", "count", "seconds"), &SteamStatsEdit::update_average_rate);
    ClassDB::bind_method(D_METHOD("unlock", "name"), &SteamStatsEdit::unlock);
    ClassDB::bind_method(D_METHOD("submit"), &SteamStatsEdit::submit);
}

Ref<SteamStatsEdit> SteamStatsEdit::append(Change change) const {
    Ref<SteamStatsEdit> branch;
    branch.instantiate();
    branch->stats_id = stats_id;
    branch->changes = changes;
    branch->changes.push_back(std::move(change));
    return branch;
}

Ref<SteamStatsEdit> SteamStatsEdit::set_int(const String &name, int64_t value) const {
    return append({name, value});
}

Ref<SteamStatsEdit> SteamStatsEdit::set_float(const String &name, double value) const {
    return append({name, value});
}

Ref<SteamStatsEdit> SteamStatsEdit::update_average_rate(const String &name, double count, double seconds) const {
    return append({name, AverageRate{count, seconds}});
}

Ref<SteamStatsEdit> SteamStatsEdit::unlock(const String &name) const {
    return append({name, std::monostate{}});
}

Ref<SteamStatsSubmissionResult> SteamStatsEdit::submit() const {
#ifdef CRIMSON_WITH_STEAM
    if (!SteamRuntime::on_main_thread()) {
        return SteamStatsSubmissionResult::create(ERR_BUSY);
    }
    const Ref<SteamStats> stats(Object::cast_to<SteamStats>(ObjectDB::get_instance(stats_id)));
    const auto active = stats.is_valid() ? stats->active_runtime() : nullptr;
    if (!active) {
        return SteamStatsSubmissionResult::create(ERR_UNAVAILABLE);
    }
    if (changes.empty()) {
        return SteamStatsSubmissionResult::create(ERR_INVALID_PARAMETER, 0, -1, "Add at least one stats edit before submitting.");
    }
    const StatsStore store = find_store(active, stats->stored_subscription);
    if (store.error != OK) {
        return SteamStatsSubmissionResult::create(store.error);
    }

    for (size_t index = 0; index < changes.size(); ++index) {
        const Change &change = changes[index];
        const CharString name = change.name.utf8();
        Error validation;
        if (const auto *value = std::get_if<int64_t>(&change.value)) {
            int32 current = 0;
            validation = valid_int(*value) ? read_int(active, name, current) : ERR_INVALID_PARAMETER;
        } else if (const auto *value = std::get_if<double>(&change.value)) {
            float current = 0.0f;
            validation = finite_float(*value) ? read_float(active, name, current) : ERR_INVALID_PARAMETER;
        } else if (const auto *rate = std::get_if<AverageRate>(&change.value)) {
            float current = 0.0f;
            validation = valid_average_rate(rate->count, rate->seconds) ? read_float(active, name, current) : ERR_INVALID_PARAMETER;
        } else {
            bool unlocked = false;
            uint32 time = 0;
            validation = SteamStats::AchievementReader(active).state(name, unlocked, time);
        }
        if (validation != OK) {
            return SteamStatsSubmissionResult::create(validation, 0, static_cast<int64_t>(index));
        }
    }

    int64_t applied = 0;
    for (const Change &change : changes) {
        Error error;
        if (const auto *value = std::get_if<int64_t>(&change.value)) {
            error = stats->set_stat_int(change.name, *value);
        } else if (const auto *value = std::get_if<double>(&change.value)) {
            error = stats->set_stat_float(change.name, *value);
        } else if (const auto *rate = std::get_if<AverageRate>(&change.value)) {
            error = stats->update_average_rate(change.name, rate->count, rate->seconds);
        } else {
            error = stats->unlock_achievement(change.name);
        }
        if (error != OK) {
            return SteamStatsSubmissionResult::create(error, applied, applied, "Steam rejected a local stats edit.");
        }
        ++applied;
    }
    if (!store.request(store.stats)) {
        return SteamStatsSubmissionResult::create(FAILED, applied, -1, "Steam rejected the request to store the cached stats.");
    }
    return SteamStatsSubmissionResult::create(OK, applied);
#else
    return SteamStatsSubmissionResult::create(ERR_UNAVAILABLE);
#endif
}

void SteamStats::_bind_methods() {
    ClassDB::bind_method(D_METHOD("is_available"), &SteamStats::is_available);
    ClassDB::bind_method(D_METHOD("edit"), &SteamStats::edit);
    ClassDB::bind_method(D_METHOD("stat_int", "name"), &SteamStats::stat_int);
    ClassDB::bind_method(D_METHOD("stat_float", "name"), &SteamStats::stat_float);
    ClassDB::bind_method(D_METHOD("set_stat_int", "name", "value"), &SteamStats::set_stat_int);
    ClassDB::bind_method(D_METHOD("set_stat_float", "name", "value"), &SteamStats::set_stat_float);
    ClassDB::bind_method(D_METHOD("update_average_rate", "name", "count", "seconds"), &SteamStats::update_average_rate);
    ClassDB::bind_method(D_METHOD("achievements"), &SteamStats::achievements);
    ClassDB::bind_method(D_METHOD("achievement", "name"), &SteamStats::achievement);
    ClassDB::bind_method(D_METHOD("unlock_achievement", "name"), &SteamStats::unlock_achievement);
    ClassDB::bind_method(D_METHOD("clear_achievement", "name"), &SteamStats::clear_achievement);
    ClassDB::bind_method(D_METHOD("show_progress", "name", "current", "maximum"), &SteamStats::show_progress);
    ClassDB::bind_method(D_METHOD("indicate_achievement_progress", "name", "current", "maximum"), &SteamStats::show_progress);
    ClassDB::bind_method(D_METHOD("store"), &SteamStats::store);
    ADD_SIGNAL(MethodInfo("stats_stored", PropertyInfo(Variant::INT, "steam_result")));
    ADD_SIGNAL(MethodInfo("achievement_stored", PropertyInfo(Variant::STRING, "name"),
            PropertyInfo(Variant::INT, "current"), PropertyInfo(Variant::INT, "maximum")));
}

void SteamStats::start_scope(const std::shared_ptr<SteamRuntime> &active,
        const std::shared_ptr<SteamClientScope> &scope) {
#ifdef CRIMSON_WITH_STEAM
    close();
    runtime = active;
    client_scope = scope;
    if (!find_user_stats(active)) {
        return;
    }
    const ObjectID id(get_instance_id());
    const uint32_t app = active->get_app_id();
    stored_subscription = active->subscribe(UserStatsStored_t::k_iCallback,
            [id, app](const void *record, size_t size) {
                if (size != sizeof(UserStatsStored_t)) {
                    return;
                }
                UserStatsStored_t event{};
                std::memcpy(&event, record, size);
                if (CGameID(event.m_nGameID).AppID() != app) {
                    return;
                }
                const Ref<SteamStats> service(Object::cast_to<SteamStats>(ObjectDB::get_instance(id)));
                if (service.is_valid() && service->client_scope && service->client_scope->active) {
                    service->emit_signal("stats_stored", static_cast<int64_t>(event.m_eResult));
                }
            });
    achievement_subscription = active->subscribe(UserAchievementStored_t::k_iCallback,
            [id, app](const void *record, size_t size) {
                if (size != sizeof(UserAchievementStored_t)) {
                    return;
                }
                UserAchievementStored_t event{};
                std::memcpy(&event, record, size);
                if (CGameID(event.m_nGameID).AppID() != app) {
                    return;
                }
                const Ref<SteamStats> service(Object::cast_to<SteamStats>(ObjectDB::get_instance(id)));
                if (service.is_valid() && service->client_scope && service->client_scope->active) {
                    const auto first = std::begin(event.m_rgchAchievementName);
                    const auto last = std::find(first, std::end(event.m_rgchAchievementName), '\0');
                    service->emit_signal("achievement_stored", String::utf8(first, last - first),
                            static_cast<int64_t>(event.m_nCurProgress), static_cast<int64_t>(event.m_nMaxProgress));
                }
            });
#endif
}

void SteamStats::close() {
#ifdef CRIMSON_WITH_STEAM
    const auto active = runtime.lock();
    runtime.reset();
    client_scope.reset();
    const uint64_t stored = std::exchange(stored_subscription, 0);
    const uint64_t progress = std::exchange(achievement_subscription, 0);
    if (active) {
        active->unsubscribe(stored);
        active->unsubscribe(progress);
    }
#endif
}

std::shared_ptr<SteamRuntime> SteamStats::active_runtime() const {
#ifdef CRIMSON_WITH_STEAM
    if (SteamRuntime::on_main_thread() && client_scope && client_scope->active) {
        return runtime.lock();
    }
#endif
    return {};
}

bool SteamStats::is_available() const {
#ifdef CRIMSON_WITH_STEAM
    return find_user_stats(active_runtime()) != nullptr;
#else
    return false;
#endif
}

Ref<SteamStatsEdit> SteamStats::edit() const {
    Ref<SteamStatsEdit> draft;
    draft.instantiate();
    draft->stats_id = get_instance_id();
    return draft;
}

Ref<SteamIntResult> SteamStats::stat_int(const String &name) const {
#ifdef CRIMSON_WITH_STEAM
    int32 value = 0;
    const Error error = read_int(active_runtime(), name.utf8(), value);
    return SteamIntResult::create(error, value);
#else
    return SteamIntResult::create(ERR_UNAVAILABLE, 0);
#endif
}

Ref<SteamFloatResult> SteamStats::stat_float(const String &name) const {
#ifdef CRIMSON_WITH_STEAM
    float value = 0.0f;
    const Error error = read_float(active_runtime(), name.utf8(), value);
    return SteamFloatResult::create(error, value);
#else
    return SteamFloatResult::create(ERR_UNAVAILABLE, 0.0);
#endif
}

Error SteamStats::set_stat_int(const String &name, int64_t value) {
#ifdef CRIMSON_WITH_STEAM
    if (!valid_int(value)) {
        return ERR_INVALID_PARAMETER;
    }
    return is_available() ? write_stat<decltype(&SteamAPI_ISteamUserStats_SetStatInt32)>(
            runtime, name, "SteamAPI_ISteamUserStats_SetStatInt32", static_cast<int32>(value)) : ERR_UNAVAILABLE;
#else
    return ERR_UNAVAILABLE;
#endif
}

Error SteamStats::set_stat_float(const String &name, double value) {
#ifdef CRIMSON_WITH_STEAM
    if (!finite_float(value)) {
        return ERR_INVALID_PARAMETER;
    }
    return is_available() ? write_stat<decltype(&SteamAPI_ISteamUserStats_SetStatFloat)>(
            runtime, name, "SteamAPI_ISteamUserStats_SetStatFloat", static_cast<float>(value)) : ERR_UNAVAILABLE;
#else
    return ERR_UNAVAILABLE;
#endif
}

Error SteamStats::update_average_rate(const String &name, double count, double seconds) {
#ifdef CRIMSON_WITH_STEAM
    if (!valid_average_rate(count, seconds)) {
        return ERR_INVALID_PARAMETER;
    }
    return is_available() ? write_stat<decltype(&SteamAPI_ISteamUserStats_UpdateAvgRateStat)>(
            runtime, name, "SteamAPI_ISteamUserStats_UpdateAvgRateStat", static_cast<float>(count), seconds) : ERR_UNAVAILABLE;
#else
    return ERR_UNAVAILABLE;
#endif
}

TypedArray<SteamAchievement> SteamStats::achievements() const {
    TypedArray<SteamAchievement> achievements;
#ifdef CRIMSON_WITH_STEAM
    const auto active = active_runtime();
    const AchievementReader reader(active);
    if (!reader.read) {
        return achievements;
    }
    const auto count = active->symbol<decltype(&SteamAPI_ISteamUserStats_GetNumAchievements)>(
            "SteamAPI_ISteamUserStats_GetNumAchievements");
    const auto name = active->symbol<decltype(&SteamAPI_ISteamUserStats_GetAchievementName)>(
            "SteamAPI_ISteamUserStats_GetAchievementName");
    if (count && name) {
        const uint32_t total = count(reader.stats);
        for (uint32_t index = 0; index < total; ++index) {
            if (const char *value = name(reader.stats, index); value && *value) {
                const Ref<SteamAchievement> entry = reader.snapshot(StringName(String::utf8(value)));
                if (entry.is_valid()) {
                    achievements.push_back(entry);
                }
            }
        }
    }
#endif
    return achievements;
}

Ref<SteamAchievement> SteamStats::achievement(const StringName &api_name) const {
#ifdef CRIMSON_WITH_STEAM
    const auto active = active_runtime();
    return AchievementReader(active).snapshot(api_name);
#else
    return {};
#endif
}

Error SteamStats::unlock_achievement(const String &name) {
#ifdef CRIMSON_WITH_STEAM
    return is_available() ? write_stat<decltype(&SteamAPI_ISteamUserStats_SetAchievement)>(
            runtime, name, "SteamAPI_ISteamUserStats_SetAchievement") : ERR_UNAVAILABLE;
#else
    return ERR_UNAVAILABLE;
#endif
}

Error SteamStats::clear_achievement(const String &name) {
#ifdef CRIMSON_WITH_STEAM
    return is_available() ? write_stat<decltype(&SteamAPI_ISteamUserStats_ClearAchievement)>(
            runtime, name, "SteamAPI_ISteamUserStats_ClearAchievement") : ERR_UNAVAILABLE;
#else
    return ERR_UNAVAILABLE;
#endif
}

Error SteamStats::show_progress(const String &name, int64_t current, int64_t maximum) {
#ifdef CRIMSON_WITH_STEAM
    if (current <= 0 || current >= maximum || maximum > (std::numeric_limits<uint32>::max)()) {
        return ERR_INVALID_PARAMETER;
    }
    return is_available() ? write_stat<decltype(&SteamAPI_ISteamUserStats_IndicateAchievementProgress)>(
            runtime, name, "SteamAPI_ISteamUserStats_IndicateAchievementProgress",
            static_cast<uint32>(current), static_cast<uint32>(maximum)) : ERR_UNAVAILABLE;
#else
    return ERR_UNAVAILABLE;
#endif
}

Error SteamStats::store() {
#ifdef CRIMSON_WITH_STEAM
    if (!SteamRuntime::on_main_thread()) {
        return ERR_BUSY;
    }
    const auto active = is_available() ? runtime.lock() : nullptr;
    const StatsStore store = find_store(active, stored_subscription);
    return store.error == OK ? (store.request(store.stats) ? OK : FAILED) : store.error;
#else
    return ERR_UNAVAILABLE;
#endif
}

} // namespace crimson::steam
