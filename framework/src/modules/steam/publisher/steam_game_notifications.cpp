// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#include "modules/steam/publisher/steam_game_notifications.hpp"
#include "modules/steam/publisher/steam_publisher_codec.hpp"
#include "modules/steam/publisher/steam_publisher_transport.hpp"

#include <godot_cpp/core/class_db.hpp>

#include <algorithm>
#include <map>
#include <set>
#include <string>
#include <utility>

namespace crimson::steam {

using namespace godot;
using publisher_codec::Json;

namespace {

// Bound all submitted fields before taking a durable claim or encoding HTTP form data.
constexpr int64_t maximum_request_bytes = 8192;
// Bound the total participants in a submitted or returned session.
constexpr int64_t maximum_participants = 16;
// Bound the localization variables in a single text value.
constexpr int64_t maximum_variables = 8;
// Bound the number of sessions in one non-paged publisher response.
constexpr int64_t maximum_sessions = 128;

// Preserve the exact bytes of validated Godot text in the vendor JSON.
std::string utf8(const String &value) {
    const CharString bytes = value.utf8();
    return std::string(bytes.get_data(), static_cast<size_t>(bytes.length()));
}

// Copy one well-formed vendor string without including parser diagnostics in logs.
String text(const std::string &value) {
    return String::utf8(value.data(), static_cast<int64_t>(value.size()));
}

// Reject control characters and bound the field's UTF-8 bytes.
bool valid_text(const std::string &value, size_t max_bytes, bool empty_allowed = false) {
    return (empty_allowed || !value.empty()) && value.size() <= max_bytes &&
            std::none_of(value.begin(), value.end(), [](unsigned char character) {
                return character < 32 || character == 127;
            });
}

// Require an operation key that can be stored and compared without Unicode normalization.
bool valid_operation_key(const String &value) {
    const std::string key = utf8(value);
    return !key.empty() && key.size() <= 128 && std::all_of(key.begin(), key.end(), [](unsigned char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
                c == '-' || c == '_' || c == '.' || c == ':';
    });
}

// Read an exact unsigned decimal context or provider identifier.
bool unsigned_text(const String &value, bool allow_zero) {
    uint64_t parsed = 0;
    return publisher_codec::unsigned_value(Json(utf8(value)), parsed) && (allow_zero || parsed != 0);
}

// Normalize a vendor uint64 ID into its canonical decimal form without narrowing it.
bool decode_id(const Json *node, bool allow_zero, String &output) {
    uint64_t parsed = 0;
    if (!node || !publisher_codec::unsigned_value(*node, parsed) || (!allow_zero && parsed == 0)) {
        return false;
    }
    output = text(std::to_string(parsed));
    return true;
}

// Encode the vendor's localized text and sort variables for stable idempotency fingerprints.
bool encode_localized(const Ref<SteamLocalizedText> &localized, Json &output) {
    if (localized.is_null()) {
        return false;
    }
    const std::string token = utf8(String(localized->get_token()));
    const auto variables = localized->get_variables();
    if (!valid_text(token, 128) || token.size() <= 1 || token.front() != '#' || variables.size() > maximum_variables) {
        return false;
    }
    std::map<std::string, std::string> ordered;
    const auto keys = variables.keys();
    for (int64_t index = 0; index < keys.size(); ++index) {
        const StringName key = keys[index];
        const std::string name = utf8(String(key));
        const std::string value = utf8(String(variables[key]));
        if (!valid_text(name, 64) || !valid_text(value, 256, true)) {
            return false;
        }
        ordered.emplace(name, value);
    }
    Json arguments = Json::array();
    for (const auto &entry : ordered) {
        arguments.push_back({{"key", entry.first}, {"value", entry.second}});
    }
    output = {{"token", token}, {"variables", std::move(arguments)}};
    return true;
}

// Decode a bounded provider localization value without accepting duplicate variable names.
bool decode_localized(const Json *input, Ref<SteamLocalizedText> &output) {
    const Json *token = publisher_codec::member(input, "token");
    const Json *variables = publisher_codec::member(input, "variables");
    if (!token || !token->is_string() || (variables && (!variables->is_array() || variables->size() > maximum_variables))) {
        return false;
    }
    const auto &name = token->get_ref<const std::string &>();
    if (!valid_text(name, 128) || name.size() <= 1 || name.front() != '#') {
        return false;
    }
    SteamLocalizedText::Variables substitutions;
    if (variables) {
        for (const auto &entry : *variables) {
            const Json *key = publisher_codec::member(&entry, "key");
            const Json *value = publisher_codec::member(&entry, "value");
            if (!key || !value || !key->is_string() || !value->is_string()) {
                return false;
            }
            const auto &key_text = key->get_ref<const std::string &>();
            const auto &value_text = value->get_ref<const std::string &>();
            if (!valid_text(key_text, 64) || !valid_text(value_text, 256, true)) {
                return false;
            }
            const StringName field(text(key_text));
            if (substitutions.has(field)) {
                return false;
            }
            substitutions[field] = text(value_text);
        }
    }
    output = SteamLocalizedText::create(StringName(text(name)), substitutions);
    return true;
}

// Translate the typed application state to Valve's exact string value.
const char *wire_state(SteamNotificationUserState::State state) {
    switch (state) {
        case SteamNotificationUserState::WAITING: return "waiting";
        case SteamNotificationUserState::READY: return "ready";
        case SteamNotificationUserState::DONE: return "done";
    }
    return nullptr;
}

// Decode one of the three documented Valve participant states.
bool decode_state(const Json *input, SteamNotificationUserState::State &state) {
    if (!input || !input->is_string()) {
        return false;
    }
    const auto &value = input->get_ref<const std::string &>();
    if (value == "waiting") {
        state = SteamNotificationUserState::WAITING;
    } else if (value == "ready") {
        state = SteamNotificationUserState::READY;
    } else if (value == "done") {
        state = SteamNotificationUserState::DONE;
    } else {
        return false;
    }
    return true;
}

// Build the fixed Steam Service request using only its typed JSON parameters.
SteamPublisherRequest notification_request(SteamPublisherEndpoint endpoint, int64_t app_id, const String &encoded) {
    SteamPublisherRequest request{endpoint};
    request.add("appid", String::num_int64(app_id));
    request.add("input_json", encoded);
    return request;
}

// Reject explicit Valve failure markers even when the transport returned HTTP 200.
bool accepts_response(const Json &object) {
    const Json *success = publisher_codec::member(&object, "success");
    if (!object.is_object() || publisher_codec::member(&object, "error") ||
            (success && (!success->is_boolean() || !success->get<bool>()))) {
        return false;
    }
    const Json *result = publisher_codec::member(&object, "result");
    uint64_t result_code = 0;
    return !result || (result->is_string() && result->get_ref<const std::string &>() == "OK") ||
            (publisher_codec::unsigned_value(*result, result_code) && result_code == 1);
}

// Read Valve's documented response object, including its empty success form.
const Json *response_object(const Json &root) {
    const Json *response = publisher_codec::member(&root, "response");
    return accepts_response(root) && response && accepts_response(*response) ? response : nullptr;
}

// Decode one app-scoped provider session without granting its participants trusted identities.
Ref<SteamNotificationSession> decode_session(const Json &record, int64_t app_id) {
    String session_id;
    String context;
    Ref<SteamLocalizedText> title;
    uint64_t response_app = 0;
    const Json *app = publisher_codec::member(&record, "appid");
    const Json *context_field = publisher_codec::member(&record, "context");
    const Json *users = publisher_codec::member(&record, "user_status");
    if (!decode_id(publisher_codec::member(&record, "sessionid"), false, session_id) || !app ||
            !publisher_codec::unsigned_value(*app, response_app) || response_app != static_cast<uint64_t>(app_id) ||
            (context_field && !decode_id(context_field, true, context)) ||
            !decode_localized(publisher_codec::member(&record, "title"), title) ||
            (users && (!users->is_array() || users->size() > maximum_participants))) {
        return {};
    }
    TypedArray<SteamNotificationParticipant> participants;
    std::set<std::string> seen;
    if (users) {
        for (const auto &user : *users) {
            String id;
            SteamNotificationUserState::State state;
            Ref<SteamLocalizedText> message;
            const Json *message_field = publisher_codec::member(&user, "message");
            if (!publisher_codec::steam_id(publisher_codec::member(&user, "steamid"), true, id) ||
                    !seen.insert(utf8(id)).second || !decode_state(publisher_codec::member(&user, "state"), state) ||
                    (message_field && !decode_localized(message_field, message))) {
                return {};
            }
            participants.push_back(SteamNotificationParticipant::create(id, state, message));
        }
    }
    return SteamNotificationSession::create(session_id, context, title, participants);
}

// Decode the response collection into a detached, bounded array for the active app.
bool decode_sessions(const PackedByteArray &body, int64_t app_id, TypedArray<SteamNotificationSession> &output) {
    const Json root = publisher_codec::parse_body(body);
    if (!accepts_response(root)) {
        return false;
    }
    const Json *response = publisher_codec::member(&root, "response");
    const Json *sessions = response && response->is_array() ? response :
            publisher_codec::member(response_object(root), "sessions");
    if (!sessions || !sessions->is_array() || sessions->size() > maximum_sessions) {
        return false;
    }
    std::set<std::string> seen;
    for (const auto &record : *sessions) {
        const auto session = decode_session(record, app_id);
        if (session.is_null() || !seen.insert(utf8(session->get_session_id())).second) {
            return false;
        }
        output.push_back(session);
    }
    return true;
}

} // namespace

// Detached wire request and public receipt inputs captured before calling the trusted store.
struct SteamNotificationMutation {
    // Fixed service route selected by the operation.
    SteamPublisherEndpoint endpoint = SteamPublisherEndpoint::NOTIFY_CREATE;
    // Durable mutation kind used by the trusted store.
    SteamNotificationStore::Action action = SteamNotificationStore::CREATE;
    // Exact existing provider target for updates and removals.
    String target;
    // Canonical JSON submitted through the fixed partner route.
    String encoded;
    // Creation context accepted by the vendor or empty for updates.
    String context;
    // Immutable title accepted by the vendor.
    Ref<SteamLocalizedText> title;
    // Detached, unprivileged participant receipts.
    TypedArray<SteamNotificationParticipant> participants;

    // Construct a receipt from the exact request Steam accepted or the store replayed.
    Ref<SteamNotificationSession> receipt(const String &provider_id) const {
        return action == SteamNotificationStore::REMOVE ? Ref<SteamNotificationSession>()
                : SteamNotificationSession::create(provider_id, context, title, participants);
    }
};

void SteamGameNotifications::_bind_methods() {
    ClassDB::bind_method(D_METHOD("request_opt_in", "user", "launch_id"), &SteamGameNotifications::request_opt_in,
            DEFVAL(String()));
    ClassDB::bind_method(D_METHOD("create", "spec", "operation_key"), &SteamGameNotifications::create);
    ClassDB::bind_method(D_METHOD("update", "session_id", "spec", "operation_key"), &SteamGameNotifications::update);
    ClassDB::bind_method(D_METHOD("sessions_for", "user"), &SteamGameNotifications::sessions_for);
    ClassDB::bind_method(D_METHOD("details", "session_id"), &SteamGameNotifications::details);
    ClassDB::bind_method(D_METHOD("remove", "session_id", "operation_key"), &SteamGameNotifications::remove);
}

Ref<SteamNotificationBoolOperation> SteamGameNotifications::request_opt_in(const Ref<SteamIdentity> &user,
        const String &launch_id) {
    Ref<SteamNotificationBoolOperation> operation;
    operation.instantiate();
    operation->pending = true;
    const auto owner = publisher_on_main_thread() ? scope.lock() : nullptr;
    auto *host = SteamPublisherHost::resolve(owner);
    if (!host || user.is_null() || !user->belongs_to(owner)) {
        operation->finish(SteamBoolResult::create(host ? ERR_UNAUTHORIZED : ERR_UNAVAILABLE));
        return operation;
    }
    if (!valid_operation_key(launch_id)) {
        operation->finish(SteamBoolResult::create(ERR_INVALID_PARAMETER, false, "Supply a trusted game launch ID."));
        return operation;
    }
    const auto store = owner->notification_store;
    if (store.is_null()) {
        operation->finish(SteamBoolResult::create(ERR_UNCONFIGURED));
        return operation;
    }
    const int64_t app_id = owner->app_id;
    const String steam_id = user->get_steam_id();
    const Error claimed = store->claim_launch_opt_in(app_id, steam_id, launch_id);
    if (claimed != OK) {
        operation->finish(SteamBoolResult::create(claimed));
        return operation;
    }
    host = SteamPublisherHost::resolve(owner);
    const Json input{{"appid", app_id}, {"steamid", utf8(steam_id)}};
    const auto request = notification_request(SteamPublisherEndpoint::NOTIFY_REQUEST, app_id, text(input.dump()));
    const Error submitted = host ? host->send(owner, request, [operation](const SteamPublisherReply &reply) {
        const Json root = publisher_codec::parse_body(reply.body);
        const Error error = reply.error != OK ? reply.error : (response_object(root) ? OK : ERR_INVALID_DATA);
        operation->finish(SteamBoolResult::create(error, error == OK));
    }) : ERR_SKIP;
    if (submitted != OK) {
        const bool released = store->release_unsent_launch_opt_in(app_id, steam_id, launch_id);
        operation->finish(SteamBoolResult::create(submitted, false,
                released ? String() : "No Steam request was scheduled. The trusted store retained the launch claim."));
    }
    return operation;
}

Error SteamGameNotifications::encode_mutation(const std::shared_ptr<SteamPublisherState> &owner,
        const Ref<SteamNotificationSessionSpec> &spec, SteamNotificationMutation &mutation) const {
    Json input{{"appid", owner->app_id}};
    if (mutation.action == SteamNotificationStore::CREATE) {
        if (spec.is_null() || !unsigned_text(spec->get_context(), true)) {
            return ERR_INVALID_PARAMETER;
        }
        mutation.context = spec->get_context();
        input["context"] = utf8(mutation.context);
    } else {
        if (!unsigned_text(mutation.target, false)) {
            return ERR_INVALID_PARAMETER;
        }
        input["sessionid"] = utf8(mutation.target);
    }
    if (mutation.action != SteamNotificationStore::REMOVE) {
        if (spec.is_null()) {
            return ERR_INVALID_PARAMETER;
        }
        mutation.title = spec->get_title();
        Json title;
        const auto users = spec->get_users();
        if (!encode_localized(mutation.title, title) || users.is_empty() || users.size() > maximum_participants) {
            return ERR_INVALID_PARAMETER;
        }
        std::set<std::string> seen;
        Json recipients = Json::array();
        for (int64_t index = 0; index < users.size(); ++index) {
            const Ref<SteamNotificationUserState> participant = users[index];
            const Ref<SteamIdentity> identity = participant.is_valid() ? participant->get_user() : Ref<SteamIdentity>();
            if (identity.is_null() || !identity->belongs_to(owner)) {
                return ERR_UNAUTHORIZED;
            }
            const String steam_id = identity->get_steam_id();
            const std::string id = utf8(steam_id);
            const char *state = wire_state(participant->get_state());
            Json message;
            if (!state || !seen.insert(id).second || !encode_localized(participant->get_message(), message)) {
                return ERR_INVALID_PARAMETER;
            }
            recipients.push_back({{"steamid", id}, {"state", state}, {"message", std::move(message)}});
            mutation.participants.push_back(SteamNotificationParticipant::create(steam_id,
                    participant->get_state(), participant->get_message()));
        }
        input["title"] = std::move(title);
        input["users"] = std::move(recipients);
    }
    const std::string encoded = input.dump();
    if (encoded.size() > maximum_request_bytes) {
        return ERR_INVALID_PARAMETER;
    }
    mutation.encoded = text(encoded);
    return OK;
}

void SteamGameNotifications::dispatch_mutation(SteamNotificationStore::Action action, const String &session_id,
        const Ref<SteamNotificationSessionSpec> &spec, const String &operation_key, MutationCompletion complete) {
    const auto owner = publisher_on_main_thread() ? scope.lock() : nullptr;
    auto *host = SteamPublisherHost::resolve(owner);
    if (!host || !valid_operation_key(operation_key)) {
        complete(host ? ERR_INVALID_PARAMETER : ERR_UNAVAILABLE, {}, {});
        return;
    }
    const auto store = owner->notification_store;
    if (store.is_null()) {
        complete(ERR_UNCONFIGURED, "Configure a trusted durable Steam notification store.", {});
        return;
    }

    SteamNotificationMutation mutation;
    mutation.action = action;
    mutation.target = session_id;
    switch (action) {
        case SteamNotificationStore::CREATE: mutation.endpoint = SteamPublisherEndpoint::NOTIFY_CREATE; break;
        case SteamNotificationStore::UPDATE: mutation.endpoint = SteamPublisherEndpoint::NOTIFY_UPDATE; break;
        case SteamNotificationStore::REMOVE: mutation.endpoint = SteamPublisherEndpoint::NOTIFY_REMOVE; break;
    }
    const Error valid = encode_mutation(owner, spec, mutation);
    if (valid != OK) {
        complete(valid, {}, {});
        return;
    }
    const int64_t app_id = owner->app_id;
    const String fingerprint = (String::num_int64(action) + "\n" + mutation.encoded).sha256_text();
    const auto claim = store->claim(app_id, operation_key, fingerprint, action, session_id);
    if (claim.is_null()) {
        complete(ERR_UNCONFIGURED, "The trusted store did not return a notification claim.", {});
        return;
    }
    host = SteamPublisherHost::resolve(owner);
    if (!host && claim->get_status() != SteamNotificationClaim::CLAIMED) {
        complete(ERR_SKIP, {}, {});
        return;
    }
    switch (claim->get_status()) {
        case SteamNotificationClaim::COMMITTED: {
            const String known = claim->get_session_id();
            if (!unsigned_text(known, false) || (action != SteamNotificationStore::CREATE && known != session_id)) {
                complete(ERR_INVALID_DATA, "The stored notification receipt does not match this request.", {});
                return;
            }
            complete(OK, {}, mutation.receipt(known));
            return;
        }
        case SteamNotificationClaim::IN_FLIGHT:
            complete(ERR_BUSY, "The trusted store still holds an unresolved notification operation.", {});
            return;
        case SteamNotificationClaim::CONFLICT:
            complete(ERR_ALREADY_IN_USE, "This operation key already identifies a different request.", {});
            return;
        case SteamNotificationClaim::DENIED:
            complete(ERR_UNAUTHORIZED, "The trusted store has no ownership for this notification session.", {});
            return;
        case SteamNotificationClaim::CLAIMED: break;
        default:
            complete(ERR_INVALID_DATA, "The trusted store returned an invalid claim decision.", {});
            return;
    }

    const auto request = notification_request(mutation.endpoint, app_id, mutation.encoded);
    const Error submitted = host ? host->send(owner, request,
            [weak = std::weak_ptr<SteamPublisherState>(owner), store, app_id, operation_key,
                    fingerprint, mutation, complete](const SteamPublisherReply &reply) {
                if (reply.error != OK) {
                    complete(reply.error, "The submitted notification outcome is unresolved in the trusted store.", {});
                    return;
                }
                const Json root = publisher_codec::parse_body(reply.body);
                const Json *response = response_object(root);
                const Json *vendor_id = publisher_codec::member(response, "sessionid");
                String provider_id = mutation.target;
                String reported_id;
                if (!response || (vendor_id && !decode_id(vendor_id, false, reported_id)) ||
                        (mutation.action == SteamNotificationStore::CREATE && reported_id.is_empty()) ||
                        (mutation.action != SteamNotificationStore::CREATE && vendor_id && reported_id != provider_id)) {
                    complete(ERR_INVALID_DATA, "Steam returned no matching notification acknowledgment. The claim remains unresolved.", {});
                    return;
                }
                if (mutation.action == SteamNotificationStore::CREATE) {
                    provider_id = reported_id;
                }
                const auto receipt = mutation.receipt(provider_id);
                if (!store->commit(app_id, operation_key, fingerprint, mutation.action, provider_id)) {
                    complete(ERR_CANT_ACQUIRE_RESOURCE, "Steam accepted the notification write. The trusted store could not commit it.", receipt);
                    return;
                }
                const Error observed = SteamPublisherHost::resolve(weak.lock()) ? OK : ERR_SKIP;
                complete(observed, {}, receipt);
            }) : ERR_UNAVAILABLE;
    if (submitted != OK) {
        const bool released = store->release_unsent(app_id, operation_key, fingerprint, action);
        complete(submitted, released ? String() : "No Steam request was scheduled. The trusted store retained its claim.", {});
    }
}

Ref<SteamNotificationSessionOperation> SteamGameNotifications::change_session(SteamNotificationStore::Action action,
        const String &session_id, const Ref<SteamNotificationSessionSpec> &spec, const String &operation_key) {
    Ref<SteamNotificationSessionOperation> operation;
    operation.instantiate();
    operation->pending = true;
    dispatch_mutation(action, session_id, spec, operation_key,
            [operation](Error error, const String &message, const Ref<SteamNotificationSession> &receipt) {
                operation->finish(SteamNotificationSessionResult::create(error, message, receipt));
            });
    return operation;
}

Ref<SteamNotificationSessionOperation> SteamGameNotifications::create(const Ref<SteamNotificationSessionSpec> &spec,
        const String &operation_key) {
    return change_session(SteamNotificationStore::CREATE, {}, spec, operation_key);
}

Ref<SteamNotificationSessionOperation> SteamGameNotifications::update(const String &session_id,
        const Ref<SteamNotificationSessionSpec> &spec, const String &operation_key) {
    return change_session(SteamNotificationStore::UPDATE, session_id, spec, operation_key);
}

Ref<SteamNotificationBoolOperation> SteamGameNotifications::remove(const String &session_id, const String &operation_key) {
    Ref<SteamNotificationBoolOperation> operation;
    operation.instantiate();
    operation->pending = true;
    dispatch_mutation(SteamNotificationStore::REMOVE, session_id, {}, operation_key,
            [operation](Error error, const String &message, const Ref<SteamNotificationSession> &) {
                operation->finish(SteamBoolResult::create(error, error == OK, message));
            });
    return operation;
}

Ref<SteamNotificationSessionsOperation> SteamGameNotifications::sessions_for(const Ref<SteamIdentity> &user) {
    Ref<SteamNotificationSessionsOperation> operation;
    operation.instantiate();
    operation->pending = true;
    const auto owner = publisher_on_main_thread() ? scope.lock() : nullptr;
    auto *host = SteamPublisherHost::resolve(owner);
    if (!host || user.is_null() || !user->belongs_to(owner)) {
        operation->finish(SteamNotificationSessionsResult::create(host ? ERR_UNAUTHORIZED : ERR_UNAVAILABLE));
        return operation;
    }
    const int64_t app_id = owner->app_id;
    const Json input{{"appid", app_id}, {"steamid", utf8(user->get_steam_id())},
            {"include_all_user_messages", false}, {"include_auth_user_message", true}};
    const auto request = notification_request(SteamPublisherEndpoint::NOTIFY_LIST, app_id, text(input.dump()));
    const Error submitted = host->send(owner, request, [operation, app_id](const SteamPublisherReply &reply) {
        TypedArray<SteamNotificationSession> sessions;
        const Error error = reply.error != OK ? reply.error :
                (decode_sessions(reply.body, app_id, sessions) ? OK : ERR_INVALID_DATA);
        operation->finish(SteamNotificationSessionsResult::create(error, {},
                error == OK ? sessions : TypedArray<SteamNotificationSession>()));
    });
    if (submitted != OK) {
        operation->finish(SteamNotificationSessionsResult::create(submitted));
    }
    return operation;
}

Ref<SteamNotificationSessionOperation> SteamGameNotifications::details(const String &session_id) {
    Ref<SteamNotificationSessionOperation> operation;
    operation.instantiate();
    operation->pending = true;
    const auto owner = publisher_on_main_thread() ? scope.lock() : nullptr;
    auto *host = SteamPublisherHost::resolve(owner);
    if (!host || !unsigned_text(session_id, false)) {
        operation->finish(SteamNotificationSessionResult::create(host ? ERR_INVALID_PARAMETER : ERR_UNAVAILABLE));
        return operation;
    }
    const auto store = owner->notification_store;
    const int64_t app_id = owner->app_id;
    if (store.is_null() || !store->owns_session(app_id, session_id)) {
        operation->finish(SteamNotificationSessionResult::create(store.is_null() ? ERR_UNCONFIGURED : ERR_UNAUTHORIZED));
        return operation;
    }
    host = SteamPublisherHost::resolve(owner);
    const Json input{{"appid", app_id}, {"sessions", Json::array({
        {{"sessionid", utf8(session_id)}, {"include_all_user_messages", true}}
    })}};
    const auto request = notification_request(SteamPublisherEndpoint::NOTIFY_DETAILS, app_id, text(input.dump()));
    const Error submitted = host ? host->send(owner, request,
            [weak = std::weak_ptr<SteamPublisherState>(owner), operation, store, app_id, session_id](const SteamPublisherReply &reply) {
        if (reply.error != OK) {
            operation->finish(SteamNotificationSessionResult::create(reply.error));
            return;
        }
        TypedArray<SteamNotificationSession> sessions;
        if (!decode_sessions(reply.body, app_id, sessions) || sessions.size() != 1) {
            operation->finish(SteamNotificationSessionResult::create(ERR_INVALID_DATA));
            return;
        }
        const Ref<SteamNotificationSession> session = sessions[0];
        if (session->get_session_id() != session_id) {
            operation->finish(SteamNotificationSessionResult::create(ERR_INVALID_DATA));
            return;
        }
        const bool still_owned = store->owns_session(app_id, session_id);
        const Error error = !SteamPublisherHost::resolve(weak.lock()) ? ERR_SKIP : (still_owned ? OK : ERR_UNAUTHORIZED);
        operation->finish(SteamNotificationSessionResult::create(error, {}, error == OK ? session : Ref<SteamNotificationSession>()));
    }) : ERR_UNAVAILABLE;
    if (submitted != OK) {
        operation->finish(SteamNotificationSessionResult::create(submitted));
    }
    return operation;
}

} // namespace crimson::steam
