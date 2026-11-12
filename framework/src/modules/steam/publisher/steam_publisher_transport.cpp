// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#include "modules/steam/publisher/steam_publisher_transport.hpp"

#include <godot_cpp/classes/http_client.hpp>
#include <godot_cpp/classes/os.hpp>
#include <godot_cpp/classes/time.hpp>
#include <godot_cpp/classes/tls_options.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>

#include <optional>
#include <string>

namespace crimson::steam {
using namespace godot;

namespace {

// The only network origin used by trusted publisher requests.
constexpr const char *partner_host = "partner.steam-api.com";
// Bound concurrent requests, wall time, and response storage independently.
constexpr size_t maximum_pending = 16;
constexpr uint64_t request_timeout_ms = 10000;
constexpr int64_t maximum_response_bytes = 256 * 1024;
// Settlement pages can contain up to a thousand orders and their line items.
constexpr int64_t maximum_report_response_bytes = 4 * 1024 * 1024;
constexpr int64_t maximum_form_bytes = 32 * 1024;

// Fixed native HTTP method and path for one approved capability.
struct Route {
    // HTTP verb accepted by the Valve endpoint.
    HTTPClient::Method method;
    // Origin-relative fixed path.
    const char *path;
};

// Select a hard-coded HTTPS path without accepting a URL from a caller.
Route route_for(SteamPublisherEndpoint endpoint, SteamPublisherSettings::Environment environment) {
    const bool sandbox = environment == SteamPublisherSettings::SANDBOX;
    switch (endpoint) {
        case SteamPublisherEndpoint::VERIFY_TICKET:
            return {HTTPClient::METHOD_GET, "/ISteamUserAuth/AuthenticateUserTicket/v1/"};
        case SteamPublisherEndpoint::CHECK_APP:
            return {HTTPClient::METHOD_GET, "/ISteamUser/CheckAppOwnership/v4/"};
        case SteamPublisherEndpoint::SUBMIT_SCORE:
            return {HTTPClient::METHOD_POST, "/ISteamLeaderboards/SetLeaderboardScore/v1/"};
        case SteamPublisherEndpoint::NOTIFY_REQUEST:
            return {HTTPClient::METHOD_POST, "/IGameNotificationsService/RequestNotifications/v1/"};
        case SteamPublisherEndpoint::NOTIFY_CREATE:
            return {HTTPClient::METHOD_POST, "/IGameNotificationsService/CreateSession/v1/"};
        case SteamPublisherEndpoint::NOTIFY_UPDATE:
            return {HTTPClient::METHOD_POST, "/IGameNotificationsService/UpdateSession/v1/"};
        case SteamPublisherEndpoint::NOTIFY_LIST:
            return {HTTPClient::METHOD_GET, "/IGameNotificationsService/EnumerateSessionsForApp/v1/"};
        case SteamPublisherEndpoint::NOTIFY_DETAILS:
            return {HTTPClient::METHOD_GET, "/IGameNotificationsService/GetSessionDetailsForApp/v1/"};
        case SteamPublisherEndpoint::NOTIFY_REMOVE:
            return {HTTPClient::METHOD_POST, "/IGameNotificationsService/DeleteSession/v1/"};
        case SteamPublisherEndpoint::REPORT_CHEATING:
            return {HTTPClient::METHOD_POST, "/ICheatReportingService/ReportPlayerCheating/v1/"};
        case SteamPublisherEndpoint::LIST_CHEATING_REPORTS:
            return {HTTPClient::METHOD_GET, "/ICheatReportingService/GetCheatingReports/v1/"};
        case SteamPublisherEndpoint::REQUEST_GAME_BAN:
            return {HTTPClient::METHOD_POST, "/ICheatReportingService/RequestPlayerGameBan/v1/"};
        case SteamPublisherEndpoint::REMOVE_GAME_BAN:
            return {HTTPClient::METHOD_POST, "/ICheatReportingService/RemovePlayerGameBan/v1/"};
        case SteamPublisherEndpoint::PURCHASE_BUYER_INFO:
            return {HTTPClient::METHOD_GET, sandbox ? "/ISteamMicroTxnSandbox/GetUserInfo/v2/" : "/ISteamMicroTxn/GetUserInfo/v2/"};
        case SteamPublisherEndpoint::INIT_PURCHASE:
            return {HTTPClient::METHOD_POST, sandbox ? "/ISteamMicroTxnSandbox/InitTxn/v3/" : "/ISteamMicroTxn/InitTxn/v3/"};
        case SteamPublisherEndpoint::QUERY_PURCHASE:
            return {HTTPClient::METHOD_GET, sandbox ? "/ISteamMicroTxnSandbox/QueryTxn/v3/" : "/ISteamMicroTxn/QueryTxn/v3/"};
        case SteamPublisherEndpoint::FINALIZE_PURCHASE:
            return {HTTPClient::METHOD_POST, sandbox ? "/ISteamMicroTxnSandbox/FinalizeTxn/v2/" : "/ISteamMicroTxn/FinalizeTxn/v2/"};
        case SteamPublisherEndpoint::REFUND_PURCHASE:
            return {HTTPClient::METHOD_POST, sandbox ? "/ISteamMicroTxnSandbox/RefundTxn/v2/" : "/ISteamMicroTxn/RefundTxn/v2/"};
        case SteamPublisherEndpoint::PURCHASE_REPORT:
            return {HTTPClient::METHOD_GET, sandbox ? "/ISteamMicroTxnSandbox/GetReport/v5/" : "/ISteamMicroTxn/GetReport/v5/"};
        case SteamPublisherEndpoint::GET_INVENTORY:
            return {HTTPClient::METHOD_GET, "/IInventoryService/GetInventory/v1/"};
        case SteamPublisherEndpoint::GRANT_INVENTORY_ITEM:
            return {HTTPClient::METHOD_POST, "/IInventoryService/AddItem/v1/"};
        case SteamPublisherEndpoint::CONSUME_INVENTORY_ITEM:
            return {HTTPClient::METHOD_POST, "/IInventoryService/ConsumeItem/v1/"};
        case SteamPublisherEndpoint::START_INVENTORY_TRADE:
            return {HTTPClient::METHOD_GET, "/ISteamEconomy/StartTrade/v1/"};
    }
    return {HTTPClient::METHOD_GET, nullptr};
}

// Encode UTF-8 or raw bytes into one application/x-www-form-urlencoded value.
void append_encoded(std::string &form, const PackedByteArray &bytes) {
    constexpr char hex[] = "0123456789ABCDEF";
    for (int64_t index = 0; index < bytes.size(); ++index) {
        const uint8_t byte = bytes[index];
        if ((byte >= '0' && byte <= '9') || (byte >= 'A' && byte <= 'Z') ||
                (byte >= 'a' && byte <= 'z') || byte == '-' || byte == '_' || byte == '.' || byte == '~') {
            form.push_back(static_cast<char>(byte));
        } else {
            form.push_back('%');
            form.push_back(hex[byte >> 4]);
            form.push_back(hex[byte & 15]);
        }
    }
}

// Build the form with credentials deliberately excluded from its fields.
String encode_form(const SteamPublisherRequest &request) {
    std::string encoded = "format=json";
    for (const auto &field : request.fields) {
        encoded.push_back('&');
        append_encoded(encoded, field.first.to_utf8_buffer());
        encoded.push_back('=');
        append_encoded(encoded, field.second);
    }
    return String::utf8(encoded.data(), encoded.size());
}

// Reject empty, oversized, or unsafe credential header values without printing them.
bool valid_header_key(const String &key) {
    if (key.is_empty() || key.length() > 128) {
        return false;
    }
    for (int64_t index = 0; index < key.length(); ++index) {
        const char32_t value = key[index];
        if (value < 0x21 || value > 0x7e) {
            return false;
        }
    }
    return true;
}

// Convert HTTP failures to stable local errors without exposing response text.
Error status_error(int status) {
    if (status >= 200 && status < 300) {
        return OK;
    }
    switch (status) {
        case 400:
        case 422:
            return ERR_INVALID_PARAMETER;
        case 401:
        case 403:
            return ERR_UNAUTHORIZED;
        case 404:
            return ERR_DOES_NOT_EXIST;
        case 408:
        case 504:
            return ERR_TIMEOUT;
        case 429:
            return ERR_BUSY;
        default:
            return FAILED;
    }
}

// One request owned by its attached host until its observation settles.
struct PendingRequest {
    // Exact scope; dropping the publisher ends local observation.
    std::weak_ptr<SteamPublisherState> owner;
    // Native connection used only by the real publisher transport.
    Ref<HTTPClient> client;
    // Fixed method selected by the endpoint definition.
    HTTPClient::Method method = HTTPClient::METHOD_GET;
    // Origin-relative request target, discarded after issuing the request.
    String target;
    // Encoded POST body, discarded after issuing the request.
    String form;
    // Request headers including the credential, discarded after issue.
    PackedStringArray headers;
    // Absolute main-thread deadline in monotonic milliseconds.
    uint64_t deadline = 0;
    // Whether the request has been issued on the connected TLS socket.
    bool sent = false;
    // Whether the initial response code and body size have been checked.
    bool inspected = false;
    // Content-Length when supplied, or -1 for a streamed response.
    int64_t expected_size = -1;
    // Response body ceiling selected by the fixed endpoint before dispatch.
    int64_t response_limit = maximum_response_bytes;
    // Bounded response bytes waiting for typed decoding.
    PackedByteArray response;
    // One typed completion owned until local settlement.
    std::function<void(const SteamPublisherReply &)> complete;
#ifdef CRIMSON_WITH_STEAM_PUBLISHER_TESTS
    // Synthetic bounded reply supplied by the test-only host override.
    SteamPublisherReply fixture;
    // Number of process ticks to delay before delivering the fake reply.
    int64_t fixture_ticks = 1;
#endif
};

// Pump one native HTTP request without following redirects or calling user code.
std::optional<SteamPublisherReply> poll_request(PendingRequest &request) {
#ifdef CRIMSON_WITH_STEAM_PUBLISHER_TESTS
    if (request.fixture_ticks > 1) {
        --request.fixture_ticks;
        return {};
    }
    return request.fixture;
#else
    const auto &client = request.client;
    if (client->poll() != OK) {
        return SteamPublisherReply{ERR_CANT_CONNECT};
    }
    const auto status = client->get_status();
    if (status == HTTPClient::STATUS_CANT_RESOLVE || status == HTTPClient::STATUS_CANT_CONNECT ||
            status == HTTPClient::STATUS_CONNECTION_ERROR || status == HTTPClient::STATUS_TLS_HANDSHAKE_ERROR) {
        return SteamPublisherReply{ERR_CANT_CONNECT};
    }
    if (!request.sent) {
        if (status == HTTPClient::STATUS_DISCONNECTED) {
            return SteamPublisherReply{ERR_CANT_CONNECT};
        }
        if (status != HTTPClient::STATUS_CONNECTED) {
            return {};
        }
        const Error sent = client->request(request.method, request.target, request.headers, request.form);
        request.target = String();
        request.form = String();
        request.headers.clear();
        if (sent != OK) {
            return SteamPublisherReply{ERR_CANT_CONNECT};
        }
        request.sent = true;
        return {};
    }
    if (!request.inspected && client->has_response()) {
        request.inspected = true;
        const Error error = status_error(client->get_response_code());
        if (error != OK) {
            return SteamPublisherReply{error};
        }
        request.expected_size = client->get_response_body_length();
        if (request.expected_size > request.response_limit) {
            return SteamPublisherReply{ERR_INVALID_DATA};
        }
    }
    if (status == HTTPClient::STATUS_BODY) {
        const PackedByteArray bytes = client->read_response_body_chunk();
        if (bytes.size() > request.response_limit - request.response.size()) {
            return SteamPublisherReply{ERR_INVALID_DATA};
        }
        request.response.append_array(bytes);
        if (client->get_status() == HTTPClient::STATUS_BODY) {
            return {};
        }
    } else if (status != HTTPClient::STATUS_CONNECTED && status != HTTPClient::STATUS_DISCONNECTED) {
        return {};
    }
    if (!request.inspected || (request.expected_size >= 0 && request.expected_size != request.response.size())) {
        return SteamPublisherReply{ERR_INVALID_DATA};
    }
    return SteamPublisherReply{OK, request.response};
#endif
}

} // namespace

// Keeps pending HTTP observations private to their exact trusted host.
struct SteamPublisherTransport {
    // Bounded list of outstanding requests.
    std::vector<PendingRequest> pending;
};

void SteamPublisherRequest::add(const String &name, const String &value) {
    add_bytes(name, value.to_utf8_buffer());
}

void SteamPublisherRequest::add_bytes(const String &name, const PackedByteArray &value) {
    fields.emplace_back(name, value);
}

bool publisher_on_main_thread() {
    const OS *os = OS::get_singleton();
    return os && os->get_thread_caller_id() == os->get_main_thread_id();
}

void SteamPublisherHost::_bind_methods() {
    GDVIRTUAL_BIND(_resolve_secret, "reference");
    GDVIRTUAL_BIND(_resolve_encrypted_ticket_key, "reference");
#ifdef CRIMSON_WITH_STEAM_PUBLISHER_TESTS
    GDVIRTUAL_BIND(_test_response, "method", "host", "target", "body", "authenticated");
    GDVIRTUAL_BIND(_test_decrypted_ownership_ticket, "ticket");
    ClassDB::bind_method(D_METHOD("_fixture_transport_enabled"), &SteamPublisherHost::_fixture_transport_enabled);
#endif
}

#ifdef CRIMSON_WITH_STEAM_PUBLISHER_TESTS
bool SteamPublisherHost::_fixture_transport_enabled() const {
    return true;
}
#endif

SteamPublisherHost::SteamPublisherHost() : transport(std::make_unique<SteamPublisherTransport>()) {}

SteamPublisherHost::~SteamPublisherHost() = default;

void SteamPublisherHost::_enter_tree() {
    accepting = true;
}

SteamPublisherHost *SteamPublisherHost::resolve(const std::shared_ptr<SteamPublisherState> &state) {
    if (!state || !state->live || !publisher_on_main_thread()) {
        return nullptr;
    }
    auto *host = Object::cast_to<SteamPublisherHost>(ObjectDB::get_instance(state->host));
    return host && host->accepting && host->is_inside_tree() && !host->is_queued_for_deletion() &&
            host->generation == state->host_generation ? host : nullptr;
}

Error SteamPublisherHost::send(const std::shared_ptr<SteamPublisherState> &state, const SteamPublisherRequest &request,
        std::function<void(const SteamPublisherReply &)> complete) {
    if (resolve(state) != this) {
        return ERR_UNAVAILABLE;
    }
    if (state->publisher_key_ref.is_empty()) {
        return ERR_UNCONFIGURED;
    }
    if (transport->pending.size() >= maximum_pending) {
        return ERR_BUSY;
    }
    const Route route = route_for(request.endpoint, state->environment);
    if (!route.path) {
        return ERR_INVALID_PARAMETER;
    }
    const String encoded = encode_form(request);
    if (encoded.length() > maximum_form_bytes) {
        return ERR_INVALID_PARAMETER;
    }

    String credential;
    if (!GDVIRTUAL_CALL(_resolve_secret, state->publisher_key_ref, credential) || !valid_header_key(credential)) {
        return ERR_UNCONFIGURED;
    }
    if (resolve(state) != this) {
        return ERR_SKIP;
    }
    if (transport->pending.size() >= maximum_pending) {
        return ERR_BUSY;
    }

    PendingRequest pending;
    pending.owner = state;
    pending.response_limit = request.endpoint == SteamPublisherEndpoint::PURCHASE_REPORT ?
            maximum_report_response_bytes : maximum_response_bytes;
    pending.method = route.method;
    pending.target = route.path;
    if (route.method == HTTPClient::METHOD_GET) {
        pending.target += "?" + encoded;
    } else {
        pending.form = encoded;
    }
#ifdef CRIMSON_WITH_STEAM_PUBLISHER_TESTS
    Dictionary fixture;
    if (!GDVIRTUAL_CALL(_test_response, route.method == HTTPClient::METHOD_GET ? String("GET") : String("POST"),
                String(partner_host), pending.target, pending.form, true, fixture)) {
        return ERR_UNCONFIGURED;
    }
    if (resolve(state) != this) {
        return ERR_SKIP;
    }
    if (transport->pending.size() >= maximum_pending) {
        return ERR_BUSY;
    }
    const Variant status = fixture.get("status", Variant());
    const Variant body = fixture.get("body", Variant());
    const Variant ticks = fixture.get("ticks", 1);
    if (status.get_type() != Variant::INT || body.get_type() != Variant::PACKED_BYTE_ARRAY ||
            ticks.get_type() != Variant::INT) {
        return ERR_INVALID_DATA;
    }
    pending.fixture = {status_error(static_cast<int>(status)), static_cast<PackedByteArray>(body)};
    pending.fixture_ticks = static_cast<int64_t>(ticks);
    if (pending.fixture.body.size() > pending.response_limit || pending.fixture_ticks < 1 || pending.fixture_ticks > 120) {
        return ERR_INVALID_DATA;
    }
    pending.target = String();
    pending.form = String();
#else
    pending.headers.append("X-WebAPI-Key: " + credential);
    pending.headers.append("Accept: application/json");
    pending.headers.append("Content-Type: application/x-www-form-urlencoded");
    pending.client.instantiate();
    pending.client->set_blocking_mode(false);
    const Error connected = pending.client->connect_to_host(partner_host, 443, TLSOptions::client());
    if (connected != OK) {
        return ERR_CANT_CONNECT;
    }
#endif
    pending.deadline = Time::get_singleton()->get_ticks_msec() + request_timeout_ms;
    pending.complete = std::move(complete);
    transport->pending.push_back(std::move(pending));
    set_process(true);
    return OK;
}

void SteamPublisherHost::_process(double delta) {
    // Remove settled requests before resuming script, which can stop or free the host.
    struct Finished {
        // Exact issuing context checked again immediately before dispatch.
        std::weak_ptr<SteamPublisherState> owner;
        // Reply ready to deliver.
        SteamPublisherReply reply;
        // Terminal continuation moved out of the pending request.
        std::function<void(const SteamPublisherReply &)> complete;
    };
    std::vector<Finished> finished;
    const uint64_t now = Time::get_singleton()->get_ticks_msec();
    auto &pending = transport->pending;
    auto request = pending.begin();
    while (request != pending.end()) {
        std::optional<SteamPublisherReply> result;
        if (resolve(request->owner.lock()) != this) {
            result = SteamPublisherReply{ERR_SKIP};
        } else if (now >= request->deadline) {
            result = SteamPublisherReply{ERR_TIMEOUT};
        } else {
            result = poll_request(*request);
        }
        if (!result) {
            ++request;
            continue;
        }
        if (request->client.is_valid()) {
            request->client->close();
        }
        finished.push_back({request->owner, std::move(*result), std::move(request->complete)});
        request = pending.erase(request);
    }
    set_process(!pending.empty());
    for (const auto &entry : finished) {
        entry.complete(resolve(entry.owner.lock()) ? entry.reply : SteamPublisherReply{ERR_SKIP});
    }
}

void SteamPublisherHost::_exit_tree() {
    accepting = false;
    ++generation;
    set_process(false);
    auto pending = std::exchange(transport->pending, {});
    for (auto &request : pending) {
        if (request.client.is_valid()) {
            request.client->close();
        }
        request.complete(SteamPublisherReply{ERR_SKIP});
    }
}

} // namespace crimson::steam
