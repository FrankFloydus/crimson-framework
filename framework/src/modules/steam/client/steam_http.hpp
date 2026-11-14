// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#pragma once

#include "modules/steam/client/steam_result.hpp"

#include <godot_cpp/core/binder_common.hpp>
#include <godot_cpp/core/object.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>
#include <godot_cpp/variant/variant.hpp>

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>

namespace crimson::steam {

class SteamRuntime;
struct SteamClientScope;
class SteamClient;
class SteamHttp;
class SteamHttpRequest;

// Result of explicitly parsing a JSON document. A valid JSON null is successful.
class SteamJsonResult : public SteamResult {
    GDCLASS(SteamJsonResult, SteamResult)

public:
    // Return the decoded JSON value, which may be null even on success.
    godot::Variant get_value() const;

protected:
    // Register the read-only dynamic JSON value.
    static void _bind_methods();

private:
    friend class SteamHttpResponse;

    // Owned JSON value from the parser.
    godot::Variant value;

    // Parse bounded response bytes and create a separate result from HTTP delivery.
    static godot::Ref<SteamJsonResult> parse(const godot::PackedByteArray &bytes);
};

// Detached metadata and bytes for a complete HTTP exchange, including HTTP errors.
class SteamHttpResponse : public godot::RefCounted {
    GDCLASS(SteamHttpResponse, godot::RefCounted)

public:
    // Return the final HTTP status code.
    int32_t get_status() const;
    // Return the complete response body, bounded by the submitted request.
    godot::PackedByteArray get_body() const;
    // Read a captured header case-insensitively, or return an empty string.
    godot::String header(const godot::String &name) const;
    // Parse the body as JSON independently of the HTTP status and transport outcome.
    godot::Ref<SteamJsonResult> json() const;
    // Return true for a complete response with a 2xx status.
    bool is_successful() const;

protected:
    // Register typed response readers.
    static void _bind_methods();

private:
    friend class SteamHttpResponseOperation;

    // Final HTTP status when a complete response exists.
    int32_t status = 0;
    // Owned response bytes, safe to read after the client scope ends.
    godot::PackedByteArray body;
    // Bounded selected response headers with lowercase keys.
    std::map<godot::String, godot::String> headers;
};

// Terminal transport outcome. Success carries a complete HTTP response.
class SteamHttpResponseResult : public SteamResult {
    GDCLASS(SteamHttpResponseResult, SteamResult)

public:
    // Return the response on transport success, or null when the request failed.
    godot::Ref<SteamHttpResponse> get_value() const;

protected:
    // Register the read-only typed response value.
    static void _bind_methods();

private:
    friend class SteamHttpResponseOperation;

    // Completed response without a native SDK handle.
    godot::Ref<SteamHttpResponse> value;

    // Publish status, diagnostics, and the optional response before exposing the result to script.
    void initialize(godot::Error error, const godot::String &diagnostic, const godot::Ref<SteamHttpResponse> &response);
};

// Cancellable handle for one submitted Steam HTTP request.
class SteamHttpResponseOperation : public godot::RefCounted {
    GDCLASS(SteamHttpResponseOperation, godot::RefCounted)

public:
    // Return whether this operation has settled.
    bool is_finished() const;
    // Return the immutable result after settlement, or null while pending.
    godot::Ref<SteamHttpResponseResult> result() const;
    // Return the completion signal or the already-settled result for use with await.
    godot::Variant wait();
    // Cancel local observation and release the request on the main thread.
    godot::Error cancel();

protected:
    // Register the typed asynchronous handle and its completion signal.
    static void _bind_methods();

private:
    friend class SteamHttp;
    friend class SteamHttpRequest;

    // Weak identity of the exact client facade that can cancel this request.
    godot::ObjectID owner;
    // Nonzero Steam stream context shared by this operation's callbacks.
    uint32_t context = 0;
    // Terminal outcome, retained by the operation for later waits.
    godot::Ref<SteamHttpResponseResult> outcome;

    // Create an already-settled operation for preflight errors.
    static godot::Ref<SteamHttpResponseOperation> resolved(godot::Error error, const godot::String &diagnostic);
    // Publish one result after all SDK and routing resources have been retired.
    void finish(godot::Error error, const godot::String &diagnostic, int32_t status = 0,
            const godot::PackedByteArray &body = {}, std::map<godot::String, godot::String> headers = {});
};

// Bounded value settings shared by immutable options branches and request drafts.
struct SteamHttpSettings {
    // Bounded, lowercased request header names and validated values.
    std::map<godot::String, godot::String> headers;
    // Selected response headers in addition to Content-Type.
    godot::PackedStringArray response_headers;
    // Absolute timeout starting at submission.
    int64_t timeout_ms = 15000;
    // Maximum body bytes to copy across Steam's callback boundary.
    int64_t response_limit = 1024 * 1024;
    // First configuration error, inherited by further branches.
    godot::String validation_error;

    // Keep the first rejected setting so send can report the original cause.
    void reject(const char *diagnostic);
    // Validate and set a case-insensitive request header on a private branch.
    void set_header(const godot::String &name, const godot::String &value);
    // Validate and set an absolute timeout on a private branch.
    void set_timeout(int64_t milliseconds);
    // Validate and set a response byte cap on a private branch.
    void set_response_limit(int64_t bytes);
    // Validate, deduplicate, and replace the additional response header selection.
    void select_response_headers(const godot::PackedStringArray &names);
};

// Configured HTTP actions. A branch holds settings and the original facade identity.
class SteamHttpOptions : public godot::RefCounted {
    GDCLASS(SteamHttpOptions, godot::RefCounted)

public:
    // HTTP methods accepted by the bounded request facade.
    enum Method {
        METHOD_GET = 1,
        METHOD_HEAD,
        METHOD_POST,
        METHOD_PUT,
        METHOD_DELETE,
        METHOD_OPTIONS,
        METHOD_PATCH,
    };

    // Return a branch that requests JSON responses.
    godot::Ref<SteamHttpOptions> accept_json() const;
    // Return a branch with a validated case-insensitive request header.
    godot::Ref<SteamHttpOptions> with_header(const godot::String &name, const godot::String &value) const;
    // Return a branch with an absolute timeout in milliseconds.
    godot::Ref<SteamHttpOptions> with_timeout(int64_t milliseconds) const;
    // Return a branch that rejects bodies larger than the chosen byte cap.
    godot::Ref<SteamHttpOptions> with_max_response_bytes(int64_t bytes) const;
    // Return a branch with a replacement set of additional response headers to capture.
    godot::Ref<SteamHttpOptions> with_response_headers(const godot::PackedStringArray &names) const;

    // Send a GET and return its result or completion signal for direct await.
    godot::Variant fetch(const godot::String &url) const;
    // Send a HEAD and return status and captured headers with an empty body.
    godot::Variant head(const godot::String &url) const;
    // Send a JSON object as POST.
    godot::Variant post(const godot::String &url, const godot::Dictionary &data) const;
    // Send a JSON object as PUT.
    godot::Variant put(const godot::String &url, const godot::Dictionary &data) const;
    // Send a JSON object as PATCH.
    godot::Variant patch(const godot::String &url, const godot::Dictionary &data) const;
    // Send a DELETE using the selected options.
    godot::Variant delete_request(const godot::String &url) const;
    // Send an OPTIONS using the selected options.
    godot::Variant options(const godot::String &url) const;
    // Prepare a request draft without allocating an SDK handle.
    godot::Ref<SteamHttpRequest> request(const godot::String &url, Method method = METHOD_GET) const;

protected:
    // Register the immutable option methods, terminals, and HTTP method enum.
    static void _bind_methods();

private:
    friend class SteamHttp;

    // Weak identity of the exact client facade that can submit the request.
    godot::ObjectID owner;
    // Settings inherited by request drafts and by further option branches.
    SteamHttpSettings settings;

    // Copy the settings into a detached options branch with the same owner identity.
    godot::Ref<SteamHttpOptions> branch() const;
    // Serialize and send a JSON object using the ordinary bounded request path.
    godot::Variant send_json(const godot::String &url, Method method, const godot::Dictionary &data) const;
};

// Detached, branchable request configuration owned by its original facade.
class SteamHttpRequest : public godot::RefCounted {
    GDCLASS(SteamHttpRequest, godot::RefCounted)

public:
    // Return a branch that requests JSON responses.
    godot::Ref<SteamHttpRequest> accept_json() const;
    // Return a branch with a validated case-insensitive request header.
    godot::Ref<SteamHttpRequest> with_header(const godot::String &name, const godot::String &value) const;
    // Return a branch with raw bytes and their content type for POST, PUT, or PATCH.
    godot::Ref<SteamHttpRequest> with_body(const godot::PackedByteArray &bytes, const godot::String &content_type) const;
    // Return a branch with an absolute timeout in milliseconds.
    godot::Ref<SteamHttpRequest> with_timeout(int64_t milliseconds) const;
    // Return a branch that rejects bodies larger than the chosen byte cap.
    godot::Ref<SteamHttpRequest> with_max_response_bytes(int64_t bytes) const;
    // Return a branch with a replacement set of additional response headers to capture.
    godot::Ref<SteamHttpRequest> with_response_headers(const godot::PackedStringArray &names) const;
    // Submit a new HTTP operation on the original client scope.
    godot::Ref<SteamHttpResponseOperation> send() const;

protected:
    // Register branchable request methods and the explicit send action.
    static void _bind_methods();

private:
    friend class SteamHttp;
    friend class SteamHttpOptions;

    // Weak identity of the facade that can open this request.
    godot::ObjectID owner;
    // Validated HTTP method chosen by the facade.
    int32_t method = 0;
    // Absolute HTTP or HTTPS URL submitted to Steam.
    godot::String url;
    // Immutable settings carried from the options branch and refined on this draft.
    SteamHttpSettings settings;
    // Raw request body when a content type was supplied.
    godot::PackedByteArray body;
    // Content type for the raw request body, or empty for no explicit body.
    godot::String content_type;

    // Copy the bounded options without changing this configuration.
    godot::Ref<SteamHttpRequest> branch() const;
};

// Main-thread Steam HTTP service for one active client scope, with default options.
class SteamHttp : public SteamHttpOptions {
    GDCLASS(SteamHttp, SteamHttpOptions)

public:
    // Construct an idle facade before attaching its client scope.
    SteamHttp();
    // Destroy the native state after the client scope has been retired.
    ~SteamHttp() override;
    // Report whether this client scope has a usable Steam HTTP interface.
    bool is_available() const;

protected:
    // Register service availability. The HTTP actions are inherited from the options surface.
    static void _bind_methods();

private:
    friend class SteamClient;
    friend class SteamHttpRequest;
    friend class SteamHttpResponseOperation;

    struct NativeState;
    // Own native request tracking without exposing SDK types to the bindings.
    std::unique_ptr<NativeState> native;

    // Attach to one started client scope without retaining that scope after stop.
    void attach(const std::shared_ptr<SteamRuntime> &runtime, const std::shared_ptr<SteamClientScope> &scope);
    // Invalidate this facade, release every request, then notify observers.
    void detach();
    // Settle requests that have passed their absolute deadline.
    void poll();
    // Validate and open one SDK request, or return a settled failure.
    godot::Ref<SteamHttpResponseOperation> send(const SteamHttpRequest &request);
    // Cancel only the current operation with this exact context.
    godot::Error cancel(uint64_t context);
    // Read one matching bounded SDK streaming chunk.
    void receive_data(const void *data, size_t size);
    // Interpret the matching typed SDK call result and settle the request.
    void complete(uint64_t context, const void *data);
    // Remove routing, release the native request, and then publish the result.
    void settle(uint64_t context, godot::Error error, const godot::String &diagnostic, int32_t status = 0,
            std::map<godot::String, godot::String> headers = {});
};

} // namespace crimson::steam

VARIANT_ENUM_CAST(crimson::steam::SteamHttpOptions::Method);
