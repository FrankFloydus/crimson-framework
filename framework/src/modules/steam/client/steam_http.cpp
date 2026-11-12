// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#include "modules/steam/client/steam_http.hpp"
#include "modules/steam/client/steam_client_scope.hpp"

#include <godot_cpp/classes/json.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/signal.hpp>

#include <cstring>
#include <string_view>
#include <utility>

#ifdef CRIMSON_WITH_STEAM
#include "modules/steam/runtime/steam_runtime.hpp"

#include <godot_cpp/classes/time.hpp>

#include <array>
#endif

namespace crimson::steam {
using namespace godot;

namespace {

// Configuration limits bound copied data before an SDK handle is opened.
constexpr int64_t maximum_url_length = 2048;
constexpr size_t maximum_headers = 24;
constexpr int64_t maximum_header_name = 128;
constexpr int64_t maximum_header_value = 2048;
constexpr int64_t maximum_response_headers = 16;
constexpr int64_t maximum_request_body = 1024 * 1024;
constexpr int64_t maximum_response_body = 4 * 1024 * 1024;
constexpr int64_t minimum_timeout = 100;
constexpr int64_t maximum_timeout = 120000;

// Reject control characters before crossing the Steam C string boundary.
bool valid_text(const String &value, bool allow_space) {
    for (int64_t index = 0; index < value.length(); ++index) {
        const char32_t c = value[index];
        if (c < (allow_space ? U' ' : U'!') || c == U'\x7f') {
            return false;
        }
    }
    return true;
}

// Require an explicit HTTP(S) authority without embedded credentials.
bool valid_url(const String &url) {
    if (url.length() > maximum_url_length || url.utf8().length() > maximum_url_length ||
            !valid_text(url, false) || url.contains("\\")) {
        return false;
    }
    const int64_t start = url.begins_with("https://") ? 8 : url.begins_with("http://") ? 7 : 0;
    if (start == 0 || start >= url.length() || url[start] == U'/' || url[start] == U'?' || url[start] == U'#') {
        return false;
    }
    for (int64_t index = start; index < url.length(); ++index) {
        const char32_t c = url[index];
        if (c == U'/' || c == U'?' || c == U'#') {
            return true;
        }
        if (c == U'@') {
            return false;
        }
    }
    return true;
}

// Validate lowercase HTTP field names using the token character set.
bool valid_header_name(const String &name) {
    if (name.is_empty() || name.length() > maximum_header_name) {
        return false;
    }
    constexpr std::u32string_view punctuation = U"!#$%&'*+-.^_`|~";
    for (int64_t index = 0; index < name.length(); ++index) {
        const char32_t c = name[index];
        if (!((c >= U'a' && c <= U'z') || (c >= U'0' && c <= U'9') ||
                    punctuation.find(c) != std::u32string_view::npos)) {
            return false;
        }
    }
    return true;
}

// Steam owns HTTP framing, host selection, and connection lifetime headers.
bool valid_request_header(const String &name) {
    return valid_header_name(name) && name != "host" && name != "content-length" &&
            name != "transfer-encoding" && name != "connection" && name != "upgrade" &&
            name != "trailer" && name != "te" && name != "proxy-connection";
}

// Validate and byte-bound a header field or content type before storing it.
bool valid_header_value(const String &value) {
    return value.length() <= maximum_header_value && valid_text(value, true) &&
            value.utf8().length() <= maximum_header_value;
}

// Steam accepts a raw request body for these methods.
bool accepts_body(int32_t method) {
    return method == SteamHttp::METHOD_POST || method == SteamHttp::METHOD_PUT ||
            method == SteamHttp::METHOD_PATCH;
}

#ifdef CRIMSON_WITH_STEAM

// Bound one facade's requests in addition to SteamRuntime's process-wide call cap.
constexpr size_t maximum_requests = 8;
// The current Steam client preserves only the low 32 bits in streaming callbacks.
// Use unique nonzero contexts across all facades and refuse further sends after wrap.
uint32_t next_http_context = 1;

// Resolve an active facade without allowing an SDK callback to retain the client scope.
Ref<SteamHttp> find_service(ObjectID id) {
    return Ref<SteamHttp>(Object::cast_to<SteamHttp>(ObjectDB::get_instance(id)));
}

// Map the public enum explicitly to the versioned Steam SDK ABI.
EHTTPMethod sdk_method(int32_t method) {
    constexpr std::array<EHTTPMethod, 7> methods = {
        k_EHTTPMethodGET, k_EHTTPMethodHEAD, k_EHTTPMethodPOST, k_EHTTPMethodPUT,
        k_EHTTPMethodDELETE, k_EHTTPMethodOPTIONS, k_EHTTPMethodPATCH,
    };
    return methods[method - SteamHttp::METHOD_GET];
}

// Copy one selected header while the exact native request is still live.
Error capture_header(ISteamHTTP &api, HTTPRequestHandle handle, const String &name,
        std::map<String, String> &headers) {
    const CharString encoded = name.utf8();
    uint32_t size = 0;
    if (!api.GetHTTPResponseHeaderSize(handle, encoded.get_data(), &size)) {
        return OK;
    }
    // The SDK's buffer size may include the trailing null terminator.
    if (size > maximum_header_value + 1) {
        return ERR_PARAMETER_RANGE_ERROR;
    }
    if (size == 0) {
        headers.emplace(name, String());
        return OK;
    }
    PackedByteArray bytes;
    if (bytes.resize(size) != OK) {
        return ERR_OUT_OF_MEMORY;
    }
    if (!api.GetHTTPResponseHeaderValue(handle, encoded.get_data(), bytes.ptrw(), size)) {
        return ERR_INVALID_DATA;
    }
    if (bytes[size - 1] == 0) {
        --size;
        if (bytes.resize(size) != OK) {
            return ERR_OUT_OF_MEMORY;
        }
    }
    if (size > maximum_header_value) {
        return ERR_PARAMETER_RANGE_ERROR;
    }
    for (uint32_t index = 0; index < size; ++index) {
        const uint8_t byte = bytes[index];
        if ((byte < ' ' && byte != '\t') || byte == 0x7f) {
            return ERR_INVALID_DATA;
        }
    }
    headers.emplace(name, bytes.get_string_from_ascii());
    return OK;
}

#endif

} // namespace

// Implementation detail for HTTP SDK ownership and bounded response assembly.
struct SteamHttp::NativeState {
#ifdef CRIMSON_WITH_STEAM
    // Exact native request and the result waiting for its terminal callback.
    struct Pending {
        // Steam HTTP request to release when this context retires.
        HTTPRequestHandle handle = INVALID_HTTPREQUEST_HANDLE;
        // Steam async call registration, or invalid before the SDK accepts send.
        SteamAPICall_t call = k_uAPICallInvalid;
        // Retained until the native request has been retired.
        Ref<SteamHttpResponseOperation> operation;
        // Monotonic local deadline, independent of Steam callback delivery.
        uint64_t deadline = 0;
        // Upper bound on bytes accepted from the response.
        uint32_t response_limit = 0;
        // Header names selected by this request before submission.
        PackedStringArray response_headers;
        // Bytes read in order from exact Steam streaming callbacks.
        PackedByteArray body;
    };

    // Weak association with the owning client's process runtime.
    std::weak_ptr<SteamRuntime> runtime;
    // Liveness shared across services in this exact client start.
    std::shared_ptr<SteamClientScope> scope;
    // Optional HTTP interface belonging to the exact runtime above.
    ISteamHTTP *api = nullptr;
    // Runtime data callback subscription, zero while this facade has no requests.
    uint64_t data_subscription = 0;
    // Active requests keyed by the exact process-wide Steam stream context.
    std::map<uint64_t, Pending> requests;
#endif
};

void SteamJsonResult::_bind_methods() {
    ClassDB::bind_method(D_METHOD("get_value"), &SteamJsonResult::get_value);
    ADD_PROPERTY(PropertyInfo(Variant::NIL, "value", PROPERTY_HINT_NONE, "",
            PROPERTY_USAGE_NIL_IS_VARIANT | PROPERTY_USAGE_READ_ONLY), "", "get_value");
}

Variant SteamJsonResult::get_value() const {
    return value;
}

Ref<SteamJsonResult> SteamJsonResult::parse(const PackedByteArray &bytes) {
    Ref<SteamJsonResult> result;
    result.instantiate();
    String text;
    Ref<JSON> parser;
    parser.instantiate();
    if (bytes.is_empty() || std::memchr(bytes.ptr(), 0, bytes.size()) ||
            text.parse_utf8(reinterpret_cast<const char *>(bytes.ptr()), bytes.size()) != OK ||
            parser->parse(text) != OK) {
        result->set_outcome(ERR_PARSE_ERROR, "The HTTP response body is not valid JSON.");
        return result;
    }
    result->set_outcome(OK);
    result->value = parser->get_data();
    return result;
}

void SteamHttpResponse::_bind_methods() {
    ClassDB::bind_method(D_METHOD("get_status"), &SteamHttpResponse::get_status);
    ClassDB::bind_method(D_METHOD("get_body"), &SteamHttpResponse::get_body);
    ClassDB::bind_method(D_METHOD("header", "name"), &SteamHttpResponse::header);
    ClassDB::bind_method(D_METHOD("json"), &SteamHttpResponse::json);
    ClassDB::bind_method(D_METHOD("is_successful"), &SteamHttpResponse::is_successful);
}

int32_t SteamHttpResponse::get_status() const {
    return status;
}

PackedByteArray SteamHttpResponse::get_body() const {
    return body;
}

String SteamHttpResponse::header(const String &name) const {
    if (name.length() > maximum_header_name) {
        return {};
    }
    const auto found = headers.find(name.to_lower());
    return found == headers.end() ? String() : found->second;
}

Ref<SteamJsonResult> SteamHttpResponse::json() const {
    return SteamJsonResult::parse(body);
}

bool SteamHttpResponse::is_successful() const {
    return status >= 200 && status < 300;
}

void SteamHttpResponseResult::_bind_methods() {
    ClassDB::bind_method(D_METHOD("get_value"), &SteamHttpResponseResult::get_value);
    ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "value", PROPERTY_HINT_RESOURCE_TYPE, "SteamHttpResponse",
            PROPERTY_USAGE_READ_ONLY, "SteamHttpResponse"), "", "get_value");
}

Ref<SteamHttpResponse> SteamHttpResponseResult::get_value() const {
    return value;
}

void SteamHttpResponseResult::initialize(Error error, const Ref<SteamHttpResponse> &response) {
    set_outcome(error);
    value = response;
}

void SteamHttpResponseOperation::_bind_methods() {
    ClassDB::bind_method(D_METHOD("is_finished"), &SteamHttpResponseOperation::is_finished);
    ClassDB::bind_method(D_METHOD("result"), &SteamHttpResponseOperation::result);
    ClassDB::bind_method(D_METHOD("wait"), &SteamHttpResponseOperation::wait);
    ClassDB::bind_method(D_METHOD("cancel"), &SteamHttpResponseOperation::cancel);
    ADD_SIGNAL(MethodInfo("completed",
            PropertyInfo(Variant::OBJECT, "result", PROPERTY_HINT_NONE, "",
                    PROPERTY_USAGE_DEFAULT, "SteamHttpResponseResult")));
}

bool SteamHttpResponseOperation::is_finished() const {
    return outcome.is_valid();
}

Ref<SteamHttpResponseResult> SteamHttpResponseOperation::result() const {
    return outcome;
}

Variant SteamHttpResponseOperation::wait() {
    if (outcome.is_valid()) {
        return outcome;
    }
    return Signal(this, "completed");
}

Error SteamHttpResponseOperation::cancel() {
#ifdef CRIMSON_WITH_STEAM
    if (!SteamRuntime::on_main_thread()) {
        return ERR_BUSY;
    }
    const auto service = find_service(owner);
    if (service.is_valid()) {
        return service->cancel(context);
    }
#endif
    return ERR_DOES_NOT_EXIST;
}

Ref<SteamHttpResponseOperation> SteamHttpResponseOperation::resolved(Error error) {
    Ref<SteamHttpResponseOperation> operation;
    operation.instantiate();
    operation->finish(error);
    return operation;
}

void SteamHttpResponseOperation::finish(Error error, int32_t status, const PackedByteArray &body,
        std::map<String, String> headers) {
    if (outcome.is_valid()) {
        return;
    }
    const Ref<SteamHttpResponseOperation> keep_alive(this);
    owner = ObjectID();
    context = 0;
    Ref<SteamHttpResponse> response;
    if (error == OK) {
        response.instantiate();
        response->status = status;
        response->body = body;
        response->headers = std::move(headers);
    }
    outcome.instantiate();
    outcome->initialize(error, response);
    emit_signal("completed", outcome);
}

void SteamHttpSettings::set_header(const String &name, const String &value) {
    const String key = name.length() <= maximum_header_name ? name.to_lower() : String();
    if (!valid_request_header(key) || !valid_header_value(value) ||
            (headers.size() >= maximum_headers && headers.find(key) == headers.end())) {
        valid = false;
        return;
    }
    headers[key] = value;
}

void SteamHttpSettings::set_timeout(int64_t milliseconds) {
    if (milliseconds < minimum_timeout || milliseconds > maximum_timeout) {
        valid = false;
        return;
    }
    timeout_ms = milliseconds;
}

void SteamHttpSettings::set_response_limit(int64_t bytes) {
    if (bytes < 1 || bytes > maximum_response_body) {
        valid = false;
        return;
    }
    response_limit = bytes;
}

void SteamHttpSettings::select_response_headers(const PackedStringArray &names) {
    PackedStringArray selected;
    for (int64_t index = 0; index < names.size(); ++index) {
        const String &name = names[index];
        const String key = name.length() <= maximum_header_name ? name.to_lower() : String();
        if (!valid_header_name(key)) {
            valid = false;
            return;
        }
        if (key == "content-type" || selected.has(key)) {
            continue;
        }
        if (selected.size() >= maximum_response_headers) {
            valid = false;
            return;
        }
        selected.push_back(key);
    }
    response_headers = selected;
}

void SteamHttpRequest::_bind_methods() {
    ClassDB::bind_method(D_METHOD("accept_json"), &SteamHttpRequest::accept_json);
    ClassDB::bind_method(D_METHOD("with_header", "name", "value"), &SteamHttpRequest::with_header);
    ClassDB::bind_method(D_METHOD("with_body", "bytes", "content_type"), &SteamHttpRequest::with_body);
    ClassDB::bind_method(D_METHOD("with_timeout", "milliseconds"), &SteamHttpRequest::with_timeout);
    ClassDB::bind_method(D_METHOD("with_max_response_bytes", "bytes"), &SteamHttpRequest::with_max_response_bytes);
    ClassDB::bind_method(D_METHOD("with_response_headers", "names"), &SteamHttpRequest::with_response_headers);
    ClassDB::bind_method(D_METHOD("send"), &SteamHttpRequest::send);
}

Ref<SteamHttpRequest> SteamHttpRequest::branch() const {
    Ref<SteamHttpRequest> copy;
    copy.instantiate();
    copy->owner = owner;
    copy->method = method;
    copy->url = url;
    copy->settings = settings;
    copy->body = body;
    copy->content_type = content_type;
    return copy;
}

Ref<SteamHttpRequest> SteamHttpRequest::accept_json() const {
    return with_header("accept", "application/json");
}

Ref<SteamHttpRequest> SteamHttpRequest::with_header(const String &name, const String &value) const {
    auto copy = branch();
    copy->settings.set_header(name, value);
    return copy;
}

Ref<SteamHttpRequest> SteamHttpRequest::with_body(
        const PackedByteArray &bytes, const String &type) const {
    auto copy = branch();
    if (!accepts_body(method) || bytes.size() > maximum_request_body || type.is_empty() || !valid_header_value(type)) {
        copy->settings.valid = false;
        return copy;
    }
    copy->body = bytes;
    copy->content_type = type;
    return copy;
}

Ref<SteamHttpRequest> SteamHttpRequest::with_timeout(int64_t milliseconds) const {
    auto copy = branch();
    copy->settings.set_timeout(milliseconds);
    return copy;
}

Ref<SteamHttpRequest> SteamHttpRequest::with_max_response_bytes(int64_t bytes) const {
    auto copy = branch();
    copy->settings.set_response_limit(bytes);
    return copy;
}

Ref<SteamHttpRequest> SteamHttpRequest::with_response_headers(const PackedStringArray &names) const {
    auto copy = branch();
    copy->settings.select_response_headers(names);
    return copy;
}

Ref<SteamHttpResponseOperation> SteamHttpRequest::send() const {
#ifdef CRIMSON_WITH_STEAM
    if (!SteamRuntime::on_main_thread()) {
        return SteamHttpResponseOperation::resolved(ERR_BUSY);
    }
    const auto service = find_service(owner);
    if (service.is_valid()) {
        return service->send(*this);
    }
#endif
    return SteamHttpResponseOperation::resolved(ERR_UNAVAILABLE);
}

void SteamHttpOptions::_bind_methods() {
    ClassDB::bind_method(D_METHOD("accept_json"), &SteamHttpOptions::accept_json);
    ClassDB::bind_method(D_METHOD("with_header", "name", "value"), &SteamHttpOptions::with_header);
    ClassDB::bind_method(D_METHOD("with_timeout", "milliseconds"), &SteamHttpOptions::with_timeout);
    ClassDB::bind_method(D_METHOD("with_max_response_bytes", "bytes"), &SteamHttpOptions::with_max_response_bytes);
    ClassDB::bind_method(D_METHOD("with_response_headers", "names"), &SteamHttpOptions::with_response_headers);
    ClassDB::bind_method(D_METHOD("fetch", "url"), &SteamHttpOptions::fetch);
    ClassDB::bind_method(D_METHOD("head", "url"), &SteamHttpOptions::head);
    ClassDB::bind_method(D_METHOD("post", "url", "data"), &SteamHttpOptions::post);
    ClassDB::bind_method(D_METHOD("put", "url", "data"), &SteamHttpOptions::put);
    ClassDB::bind_method(D_METHOD("patch", "url", "data"), &SteamHttpOptions::patch);
    ClassDB::bind_method(D_METHOD("delete", "url"), &SteamHttpOptions::delete_request);
    ClassDB::bind_method(D_METHOD("options", "url"), &SteamHttpOptions::options);
    ClassDB::bind_method(D_METHOD("request", "url", "method"), &SteamHttpOptions::request, DEFVAL(METHOD_GET));
    BIND_ENUM_CONSTANT(METHOD_GET);
    BIND_ENUM_CONSTANT(METHOD_HEAD);
    BIND_ENUM_CONSTANT(METHOD_POST);
    BIND_ENUM_CONSTANT(METHOD_PUT);
    BIND_ENUM_CONSTANT(METHOD_DELETE);
    BIND_ENUM_CONSTANT(METHOD_OPTIONS);
    BIND_ENUM_CONSTANT(METHOD_PATCH);
}

Ref<SteamHttpOptions> SteamHttpOptions::branch() const {
    Ref<SteamHttpOptions> copy;
    copy.instantiate();
    copy->owner = owner;
    copy->settings = settings;
    return copy;
}

Ref<SteamHttpOptions> SteamHttpOptions::accept_json() const {
    return with_header("accept", "application/json");
}

Ref<SteamHttpOptions> SteamHttpOptions::with_header(const String &name, const String &value) const {
    auto copy = branch();
    copy->settings.set_header(name, value);
    return copy;
}

Ref<SteamHttpOptions> SteamHttpOptions::with_timeout(int64_t milliseconds) const {
    auto copy = branch();
    copy->settings.set_timeout(milliseconds);
    return copy;
}

Ref<SteamHttpOptions> SteamHttpOptions::with_max_response_bytes(int64_t bytes) const {
    auto copy = branch();
    copy->settings.set_response_limit(bytes);
    return copy;
}

Ref<SteamHttpOptions> SteamHttpOptions::with_response_headers(const PackedStringArray &names) const {
    auto copy = branch();
    copy->settings.select_response_headers(names);
    return copy;
}

Variant SteamHttpOptions::fetch(const String &url) const {
    return request(url)->send()->wait();
}

Variant SteamHttpOptions::head(const String &url) const {
    return request(url, METHOD_HEAD)->send()->wait();
}

Variant SteamHttpOptions::post(const String &url, const Dictionary &data) const {
    return send_json(url, METHOD_POST, data);
}

Variant SteamHttpOptions::put(const String &url, const Dictionary &data) const {
    return send_json(url, METHOD_PUT, data);
}

Variant SteamHttpOptions::patch(const String &url, const Dictionary &data) const {
    return send_json(url, METHOD_PATCH, data);
}

Variant SteamHttpOptions::delete_request(const String &url) const {
    return request(url, METHOD_DELETE)->send()->wait();
}

Variant SteamHttpOptions::options(const String &url) const {
    return request(url, METHOD_OPTIONS)->send()->wait();
}

Variant SteamHttpOptions::send_json(const String &url, Method method, const Dictionary &data) const {
    auto draft = request(url, method);
    if (!draft->settings.valid) {
        return draft->send()->wait();
    }
    const String json = JSON::stringify(data, {}, false, true);
    if (json.length() > maximum_request_body) {
        draft->settings.valid = false;
        return draft->send()->wait();
    }
    return draft->with_body(json.to_utf8_buffer(), "application/json")->send()->wait();
}

Ref<SteamHttpRequest> SteamHttpOptions::request(const String &url, Method method) const {
    Ref<SteamHttpRequest> config;
    config.instantiate();
    config->owner = owner;
    config->method = method;
    config->settings = settings;
    config->settings.valid = settings.valid && method >= METHOD_GET && method <= METHOD_PATCH && valid_url(url);
    if (config->settings.valid) {
        config->url = url;
    }
    return config;
}

SteamHttp::SteamHttp() : native(std::make_unique<NativeState>()) {}
SteamHttp::~SteamHttp() = default;

void SteamHttp::_bind_methods() {
    ClassDB::bind_method(D_METHOD("is_available"), &SteamHttp::is_available);
}

bool SteamHttp::is_available() const {
#ifdef CRIMSON_WITH_STEAM
    return SteamRuntime::on_main_thread() && native->scope && native->scope->active &&
            native->api && !native->runtime.expired();
#else
    return false;
#endif
}

void SteamHttp::attach(const std::shared_ptr<SteamRuntime> &runtime,
        const std::shared_ptr<SteamClientScope> &scope) {
#ifdef CRIMSON_WITH_STEAM
    owner = ObjectID(get_instance_id());
    const auto http = runtime->symbol<decltype(&SteamAPI_SteamHTTP_v003)>("SteamAPI_SteamHTTP_v003");
    native->runtime = runtime;
    native->scope = scope;
    native->api = http ? http() : nullptr;
#endif
}

void SteamHttp::detach() {
#ifdef CRIMSON_WITH_STEAM
    const Ref<SteamHttp> keep_alive(this);
    const auto runtime = native->runtime.lock();
    native->runtime.reset();
    native->scope.reset();
    auto *api = std::exchange(native->api, nullptr);
    const auto subscription = std::exchange(native->data_subscription, 0);
    const auto pending = std::exchange(native->requests, {});
    if (runtime && api) {
        runtime->unsubscribe(subscription);
        for (const auto &[context, request] : pending) {
            runtime->unwatch_call(request.call);
            api->ReleaseHTTPRequest(request.handle);
        }
    }
    for (const auto &[context, request] : pending) {
        request.operation->finish(ERR_SKIP);
    }
#endif
}

void SteamHttp::poll() {
#ifdef CRIMSON_WITH_STEAM
    if (native->requests.empty()) {
        return;
    }
    const uint64_t now = Time::get_singleton()->get_ticks_msec();
    std::array<uint64_t, maximum_requests> expired;
    size_t count = 0;
    for (const auto &[context, pending] : native->requests) {
        if (now >= pending.deadline) {
            expired[count++] = context;
        }
    }
    for (size_t index = 0; index < count; ++index) {
        settle(expired[index], ERR_TIMEOUT);
    }
#endif
}

Ref<SteamHttpResponseOperation> SteamHttp::send(const SteamHttpRequest &request) {
#ifdef CRIMSON_WITH_STEAM
    const auto runtime = native->runtime.lock();
    ISteamHTTP *api = native->api;
    if (!runtime || !api || !native->scope || !native->scope->active) {
        return SteamHttpResponseOperation::resolved(ERR_UNAVAILABLE);
    }
    if (!request.settings.valid) {
        return SteamHttpResponseOperation::resolved(ERR_INVALID_PARAMETER);
    }
    if (next_http_context == 0 || native->requests.size() >= maximum_requests || !runtime->can_track_call()) {
        return SteamHttpResponseOperation::resolved(ERR_BUSY);
    }

    const HTTPRequestHandle handle = api->CreateHTTPRequest(sdk_method(request.method), request.url.utf8().get_data());
    if (handle == INVALID_HTTPREQUEST_HANDLE) {
        return SteamHttpResponseOperation::resolved(ERR_CANT_CREATE);
    }
    Ref<SteamHttpResponseOperation> operation;
    operation.instantiate();
    const uint32_t context = next_http_context++;
    bool configured = api->SetHTTPRequestContextValue(handle, context) &&
            api->SetHTTPRequestAbsoluteTimeoutMS(handle, static_cast<uint32_t>(request.settings.timeout_ms)) &&
            (!request.url.begins_with("https://") || api->SetHTTPRequestRequiresVerifiedCertificate(handle, true));
    for (const auto &[name, value] : request.settings.headers) {
        if (!configured) {
            break;
        }
        if (name != "content-type" || request.content_type.is_empty()) {
            configured = api->SetHTTPRequestHeaderValue(handle, name.utf8().get_data(), value.utf8().get_data());
        }
    }
    if (configured && !request.content_type.is_empty()) {
        PackedByteArray body = request.body;
        uint8_t empty = 0;
        configured = api->SetHTTPRequestRawPostBody(handle, request.content_type.utf8().get_data(),
                body.is_empty() ? &empty : body.ptrw(), static_cast<uint32_t>(body.size()));
    }
    if (!configured) {
        api->ReleaseHTTPRequest(handle);
        operation->finish(ERR_INVALID_PARAMETER);
        return operation;
    }

    const ObjectID self(get_instance_id());
    if (native->data_subscription == 0) {
        native->data_subscription = runtime->subscribe(HTTPRequestDataReceived_t::k_iCallback,
                [self](const void *data, size_t size) {
                    const auto service = find_service(self);
                    if (service.is_valid()) {
                        service->receive_data(data, size);
                    }
                });
        if (native->data_subscription == 0) {
            api->ReleaseHTTPRequest(handle);
            operation->finish(ERR_BUSY);
            return operation;
        }
    }
    operation->owner = self;
    operation->context = context;
    auto &pending = native->requests[context];
    pending.handle = handle;
    pending.operation = operation;
    pending.deadline = Time::get_singleton()->get_ticks_msec() + static_cast<uint64_t>(request.settings.timeout_ms);
    pending.response_limit = static_cast<uint32_t>(request.settings.response_limit);
    pending.response_headers = request.settings.response_headers;
    SteamAPICall_t call = k_uAPICallInvalid;
    if (!api->SendHTTPRequestAndStreamResponse(handle, &call) ||
            !runtime->watch_call(call, HTTPRequestCompleted_t::k_iCallback, sizeof(HTTPRequestCompleted_t),
                    [self, context](const void *data) {
                        const auto service = find_service(self);
                        if (service.is_valid()) {
                            service->complete(context, data);
                        }
                    })) {
        settle(context, ERR_CANT_CONNECT);
    } else {
        pending.call = call;
    }
    return operation;
#else
    return SteamHttpResponseOperation::resolved(ERR_UNAVAILABLE);
#endif
}

Error SteamHttp::cancel(uint64_t context) {
#ifdef CRIMSON_WITH_STEAM
    if (native->requests.find(context) != native->requests.end()) {
        settle(context, ERR_SKIP);
        return OK;
    }
#endif
    return ERR_DOES_NOT_EXIST;
}

void SteamHttp::receive_data(const void *data, size_t size) {
#ifdef CRIMSON_WITH_STEAM
    if (!data || size != sizeof(HTTPRequestDataReceived_t)) {
        return;
    }
    HTTPRequestDataReceived_t event{};
    std::memcpy(&event, data, sizeof(event));
    const auto found = native->requests.find(event.m_ulContextValue);
    if (found == native->requests.end() || found->second.handle != event.m_hRequest) {
        return;
    }
    auto &pending = found->second;
    if (Time::get_singleton()->get_ticks_msec() >= pending.deadline) {
        settle(event.m_ulContextValue, ERR_TIMEOUT);
        return;
    }
    const auto assembled = static_cast<uint32_t>(pending.body.size());
    if (event.m_cOffset != assembled) {
        settle(event.m_ulContextValue, ERR_INVALID_DATA);
        return;
    }
    if (event.m_cBytesReceived > pending.response_limit - assembled) {
        settle(event.m_ulContextValue, ERR_PARAMETER_RANGE_ERROR);
        return;
    }
    if (event.m_cBytesReceived == 0) {
        return;
    }
    if (pending.body.resize(assembled + event.m_cBytesReceived) != OK) {
        settle(event.m_ulContextValue, ERR_OUT_OF_MEMORY);
        return;
    }
    if (!native->api->GetHTTPStreamingResponseBodyData(
                event.m_hRequest, assembled, pending.body.ptrw() + assembled, event.m_cBytesReceived)) {
        settle(event.m_ulContextValue, ERR_INVALID_DATA);
    }
#endif
}

void SteamHttp::complete(uint64_t context, const void *data) {
#ifdef CRIMSON_WITH_STEAM
    const auto found = native->requests.find(context);
    if (found == native->requests.end()) {
        return;
    }
    if (Time::get_singleton()->get_ticks_msec() >= found->second.deadline) {
        settle(context, ERR_TIMEOUT);
        return;
    }
    HTTPRequestCompleted_t event{};
    if (data) {
        std::memcpy(&event, data, sizeof(event));
        if (event.m_ulContextValue != context || event.m_hRequest != found->second.handle) {
            settle(context, ERR_INVALID_DATA);
            return;
        }
    }
    if (!data || !event.m_bRequestSuccessful) {
        bool timed_out = false;
        native->api->GetHTTPRequestWasTimedOut(found->second.handle, &timed_out);
        settle(context, timed_out ? ERR_TIMEOUT : ERR_CANT_CONNECT);
        return;
    }
    const int32_t status = event.m_eStatusCode;
    // Steam reports zero body size for streamed responses even when it sent data callbacks.
    if (event.m_unBodySize > found->second.response_limit) {
        settle(context, ERR_PARAMETER_RANGE_ERROR);
        return;
    }
    if (status < 100 || status > 599 ||
            (event.m_unBodySize != 0 && event.m_unBodySize != found->second.body.size())) {
        settle(context, ERR_INVALID_DATA);
        return;
    }
    std::map<String, String> headers;
    const auto &pending = found->second;
    Error header_error = capture_header(*native->api, pending.handle, "content-type", headers);
    for (int64_t index = 0; header_error == OK && index < pending.response_headers.size(); ++index) {
        header_error = capture_header(*native->api, pending.handle, pending.response_headers[index], headers);
    }
    if (header_error != OK) {
        settle(context, header_error);
        return;
    }
    settle(context, OK, status, std::move(headers));
#endif
}

void SteamHttp::settle(uint64_t context, Error error, int32_t status, std::map<String, String> headers) {
#ifdef CRIMSON_WITH_STEAM
    const Ref<SteamHttp> keep_alive(this);
    auto request = native->requests.extract(context);
    if (request.empty()) {
        return;
    }
    auto &pending = request.mapped();
    if (const auto runtime = native->runtime.lock()) {
        runtime->unwatch_call(pending.call);
        native->api->ReleaseHTTPRequest(pending.handle);
        if (native->requests.empty()) {
            runtime->unsubscribe(std::exchange(native->data_subscription, 0));
        }
    }
    pending.operation->finish(error, status, pending.body, std::move(headers));
#endif
}

} // namespace crimson::steam
