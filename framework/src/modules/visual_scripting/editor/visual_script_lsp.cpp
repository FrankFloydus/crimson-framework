// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#include "modules/visual_scripting/editor/visual_script_lsp.hpp"

#ifdef DEBUG_ENABLED

#include <godot_cpp/classes/editor_interface.hpp>
#include <godot_cpp/classes/editor_settings.hpp>
#include <godot_cpp/classes/json.hpp>
#include <godot_cpp/classes/os.hpp>
#include <godot_cpp/classes/project_settings.hpp>
#include <godot_cpp/classes/time.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>

#include <algorithm>
#include <atomic>
#include <utility>

namespace crimson::visual_scripting {
namespace {

using namespace godot;

/// Bound the size of a server response and the data processed on any frame.
constexpr int64_t max_packet = 16 * 1024 * 1024;
/// Stay below the stock server's 4 MiB reader and cap bytes queued on this side too.
constexpr int64_t max_request = 4 * 1024 * 1024 - 4096;
/// Limit queued source revisions when the parser cannot catch up.
constexpr size_t max_pending_documents = 32;
/// Give the editor time to accept and initialize this client without blocking.
constexpr uint64_t startup_timeout_ms = 5000;
/// Recover from a missing response rather than misattributing future empty diagnostics.
constexpr uint64_t response_timeout_ms = 12000;
/// Reserve synthetic paths between Custom Script editors in the same process.
std::atomic<uint64_t> document_counter{0};
/// Allow fixtures or editors launched with an explicit server port to locate it.
constexpr char port_option[] = "--crimson-lsp-port=";

/// Collect a small protocol record from pairs without spreading manual Dictionary writes.
template <typename... Items>
Dictionary fields(Items &&...items) {
    const Array pairs = Array::make(std::forward<Items>(items)...);
    Dictionary result;
    for (int64_t item = 0; item < pairs.size(); item += 2) {
        result[pairs[item]] = pairs[item + 1];
    }
    return result;
}

/// Read only a numeric port and restrict transport to this workstation.
int32_t editor_lsp_port() {
    for (const String &argument : OS::get_singleton()->get_cmdline_user_args()) {
        if (argument.begins_with(port_option)) {
            const String value = argument.trim_prefix(port_option);
            const int64_t port = value.is_valid_int() ? value.to_int() : 0;
            return port > 0 && port <= 65535 ? static_cast<int32_t>(port) : 0;
        }
    }
    const Ref<EditorSettings> settings = EditorInterface::get_singleton()->get_editor_settings();
    const int64_t port = settings.is_valid() && settings->has_setting("network/language_server/remote_port") ?
            int64_t(settings->get_setting("network/language_server/remote_port")) : 6005;
    return port > 0 && port <= 65535 ? static_cast<int32_t>(port) : 0;
}

/// Encode disjoint path segments while preserving the slash and Windows drive separator.
String file_uri(const String &path) {
    PackedStringArray pieces = path.replace("\\", "/").split("/");
    for (int64_t index = 0; index < pieces.size(); ++index) {
        if (!(index == 0 && pieces[index].ends_with(":"))) {
            pieces.set(index, pieces[index].uri_encode());
        }
    }
    return String("file:///") + String("/").join(pieces);
}

/// Find the first complete LSP header without decoding UTF-8 payload bytes prematurely.
int64_t header_size(const PackedByteArray &bytes) {
    for (int64_t index = 0; index + 3 < bytes.size(); ++index) {
        if (bytes[index] == '\r' && bytes[index + 1] == '\n' && bytes[index + 2] == '\r' && bytes[index + 3] == '\n') {
            return index + 4;
        }
        if (index > 4096) break;
    }
    return 0;
}

/// Read Content-Length as a byte count and reject malformed or unreasonably large frames.
int64_t content_size(const PackedByteArray &bytes, int64_t header_end) {
    const String header = bytes.slice(0, header_end - 4).get_string_from_ascii();
    for (const String &line : header.split("\r\n")) {
        if (line.to_lower().begins_with("content-length:")) {
            const String value = line.substr(15).strip_edges();
            const int64_t size = value.is_valid_int() ? value.to_int() : 0;
            return size > 0 && size <= max_packet ? size : -1;
        }
    }
    return -1;
}

}

void VisualScriptLsp::open(const String &generated_source) {
    close();
    const int32_t port = editor_lsp_port();
    if (port == 0) {
        fail("Godot's language server port is invalid.");
        return;
    }
    root_path = ProjectSettings::get_singleton()->globalize_path("res://").trim_suffix("/");
    root_uri = file_uri(root_path);
    const String name = String(".crimson_code_") + String::num_uint64(OS::get_singleton()->get_process_id()) +
            "_" + String::num_uint64(++document_counter) + ".gd";
    uri = file_uri(root_path.path_join(name));
    source = generated_source;
    revision = 1;
    deadline = Time::get_singleton()->get_ticks_msec() + startup_timeout_ms;
    peer.instantiate();
    if (peer->connect_to_host("127.0.0.1", port) != OK) {
        fail("Could not connect to Godot's local language server.");
        return;
    }
    phase = Phase::CONNECTING;
}

void VisualScriptLsp::update(const String &generated_source) {
    if (phase == Phase::CLOSED || source == generated_source) return;
    source = generated_source;
    ++revision;
    publish_at = Time::get_singleton()->get_ticks_msec() + 150;
    completion.reset();
}

void VisualScriptLsp::complete(int32_t line, int32_t column) {
    if (phase == Phase::CLOSED || line < 0 || column < 0) return;
    completion = Completion{0, revision, line, column};
}

bool VisualScriptLsp::send(const char *method, const Dictionary &parameters, int64_t id) {
    Dictionary message = fields("jsonrpc", "2.0", "method", method, "params", parameters);
    if (id > 0) message["id"] = id;
    const PackedByteArray payload = JSON::stringify(message).to_utf8_buffer();
    const PackedByteArray header = (String("Content-Length: ") + String::num_int64(payload.size()) + "\r\n\r\n").to_utf8_buffer();
    if (header.size() + payload.size() > max_request || outbound.size() - sent + header.size() + payload.size() > max_packet) {
        return false;
    }
    if (sent > 0) {
        outbound = outbound.slice(sent);
        sent = 0;
    }
    outbound.append_array(header);
    outbound.append_array(payload);
    return true;
}

bool VisualScriptLsp::exchange() {
    if (sent < outbound.size()) {
        const int64_t end = std::min<int64_t>(outbound.size(), sent + 256 * 1024);
        const Array written = peer->put_partial_data(outbound.slice(sent, end));
        if (written.size() != 2 || int64_t(written[0]) != OK) return false;
        sent += int64_t(written[1]);
        if (sent == outbound.size()) {
            outbound.clear();
            sent = 0;
        }
    }
    const int32_t ready = peer->get_available_bytes();
    if (ready > 0) {
        const Array chunk = peer->get_partial_data(std::min(ready, 256 * 1024));
        if (chunk.size() != 2 || int64_t(chunk[0]) != OK) return false;
        inbound.append_array(chunk[1]);
    }
    return receive();
}

bool VisualScriptLsp::receive() {
    while (!inbound.is_empty()) {
        const int64_t header = header_size(inbound);
        if (header == 0) return inbound.size() <= 4096;
        const int64_t count = content_size(inbound, header);
        if (count < 0) return false;
        if (inbound.size() < header + count) return true;
        const Variant packet = JSON::parse_string(inbound.slice(header, header + count).get_string_from_utf8());
        inbound = inbound.slice(header + count);
        if (packet.get_type() != Variant::DICTIONARY) return false;
        dispatch(packet);
        if (phase == Phase::CLOSED) return true;
    }
    return true;
}

void VisualScriptLsp::dispatch(const Dictionary &message) {
    const String method = message.get("method", String());
    if (method == "gdscript_client/changeWorkspace") {
        fail("The local Godot language server belongs to a different project.");
        return;
    }
    const int64_t id = message.get("id", 0);
    const Variant result = message.get("result", Variant());
    if (id == 1 && phase == Phase::INITIALIZING) {
        if (result.get_type() != Variant::DICTIONARY ||
                !Dictionary(Dictionary(result).get("capabilities", Dictionary())).has("completionProvider")) {
            fail("The local endpoint did not initialize as the Godot language server.");
            return;
        }
        phase = Phase::READY;
        if (!send("initialized")) fail("Could not queue language server initialization.");
        return;
    }
    if (completion && id != 0 && id == completion->id && completion->revision == revision) {
        completion.reset();
        if (on_completion) {
            on_completion(result.get_type() == Variant::ARRAY ? Array(result) : result.get_type() == Variant::DICTIONARY ?
                    Array(Dictionary(result).get("items", Array())) : Array());
        }
        return;
    }
    if (method != "textDocument/publishDiagnostics") return;
    const Dictionary params = message.get("params", Dictionary());
    const String target_uri = params.get("uri", String());
    if (uri.uri_decode().nocasecmp_to(target_uri.uri_decode()) != 0 || awaiting_diagnostics.empty()) return;
    // Send each change in order. The stock server responds once per update on this peer and does not echo the version.
    const int64_t diagnosed = awaiting_diagnostics.front();
    awaiting_diagnostics.pop_front();
    diagnostics_deadline = awaiting_diagnostics.empty() ? 0 : Time::get_singleton()->get_ticks_msec() + response_timeout_ms;
    if (diagnosed == revision && on_diagnostics) on_diagnostics(params.get("diagnostics", Array()));
}

void VisualScriptLsp::publish() {
    if (awaiting_diagnostics.size() >= max_pending_documents) {
        fail("Godot's language server is falling behind. Reopen Edit Code to reconnect.");
        return;
    }
    const Dictionary document = fields("uri", uri, "version", revision);
    bool queued;
    if (published == 0) {
        Dictionary initial = document;
        initial["languageId"] = "gdscript";
        initial["text"] = source;
        queued = send("textDocument/didOpen", fields("textDocument", initial));
    } else {
        queued = send("textDocument/didChange", fields("textDocument", document, "contentChanges", Array::make(fields("text", source))));
    }
    if (!queued) {
        fail("This source update exceeds the language server buffer. Shorten the code and reopen Edit Code.");
        return;
    }
    // A typing debounce can skip revisions, so track exactly which versions the server will acknowledge.
    if (awaiting_diagnostics.empty()) diagnostics_deadline = Time::get_singleton()->get_ticks_msec() + response_timeout_ms;
    awaiting_diagnostics.push_back(revision);
    published = revision;
}

void VisualScriptLsp::poll() {
    if (phase == Phase::CLOSED) return;
    const uint64_t now = Time::get_singleton()->get_ticks_msec();
    if (peer->poll() != OK || peer->get_status() == StreamPeerTCP::STATUS_ERROR || peer->get_status() == StreamPeerTCP::STATUS_NONE) {
        fail("Godot's local language server is unavailable.");
        return;
    }
    if (phase == Phase::CONNECTING && peer->get_status() == StreamPeerTCP::STATUS_CONNECTED) {
        phase = Phase::INITIALIZING;
        if (!send("initialize", fields("processId", OS::get_singleton()->get_process_id(), "rootUri", root_uri, "rootPath", root_path,
                                       "capabilities", Dictionary()), 1)) {
            fail("Could not queue language server initialization.");
            return;
        }
    }
    if (phase != Phase::CONNECTING && !exchange()) {
        fail("The Godot language server connection closed.");
        return;
    }
    if (phase == Phase::CLOSED) return;
    if (phase != Phase::READY && now >= deadline) {
        fail("Godot's local language server did not respond.");
        return;
    }
    if (phase == Phase::READY && ((diagnostics_deadline != 0 && now >= diagnostics_deadline) ||
            (completion && completion->id > 0 && now >= completion_deadline))) {
        fail("Godot's language server did not respond. Reopen Edit Code to reconnect.");
        return;
    }
    if (phase == Phase::READY && published != revision && (completion || now >= publish_at)) publish();
    if (phase == Phase::READY && completion && completion->id == 0) {
        completion->id = next_id++;
        completion_deadline = now + response_timeout_ms;
        if (!send("textDocument/completion",
                fields("textDocument", fields("uri", uri), "position", fields("line", completion->line, "character", completion->column)),
                completion->id)) {
            fail("Could not queue language server completion.");
            return;
        }
    }
}

void VisualScriptLsp::fail(const String &message) {
    close();
    if (on_unavailable) on_unavailable(message);
}

void VisualScriptLsp::close() {
    if (peer.is_valid()) {
        if (phase == Phase::READY && published > 0 && sent == outbound.size()) {
            if (send("textDocument/didClose", fields("textDocument", fields("uri", uri)))) peer->put_partial_data(outbound);
        }
        // Disconnect also discards any unsent virtual documents and parser state on this peer.
        peer->disconnect_from_host();
        peer.unref();
    }
    phase = Phase::CLOSED;
    inbound.clear();
    outbound.clear();
    sent = 0;
    published = 0;
    awaiting_diagnostics.clear();
    revision = 0;
    next_id = 2;
    deadline = 0;
    diagnostics_deadline = 0;
    completion_deadline = 0;
    publish_at = 0;
    source = String();
    completion.reset();
}

}

#endif
