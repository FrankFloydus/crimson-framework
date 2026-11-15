// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#pragma once

#ifdef DEBUG_ENABLED

#include <godot_cpp/classes/stream_peer_tcp.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>

#include <deque>
#include <functional>
#include <optional>

namespace crimson::visual_scripting {

/// A private asynchronous client of the local stock Godot editor language server.
class VisualScriptLsp {
    /// Tracks a completion cursor in the generated document and the source version it belongs to.
    struct Completion {
        /// JSON-RPC response identifier.
        int64_t id = 0;
        /// Document version sent with this request.
        int64_t revision = 0;
        /// Zero-based generated document line.
        int32_t line = 0;
        /// Zero-based Godot code-point column.
        int32_t column = 0;
    };

    /// Distinguish transport startup from initialized document ownership.
    enum class Phase { CLOSED, CONNECTING, INITIALIZING, READY };
    /// Current transport and protocol phase.
    Phase phase = Phase::CLOSED;
    /// One dedicated peer ensures separate document ownership from external IDEs.
    godot::Ref<godot::StreamPeerTCP> peer;
    /// Incoming bytes not yet consumed as complete messages.
    godot::PackedByteArray inbound;
    /// Outgoing bytes not yet written to the peer.
    godot::PackedByteArray outbound;
    /// Next byte to send in the current outbound buffer.
    int64_t sent = 0;
    /// Next request ID on this connection.
    int64_t next_id = 2;
    /// Deadline for TCP connection and initialize reply.
    uint64_t deadline = 0;
    /// Deadline for the first outstanding document diagnostic.
    uint64_t diagnostics_deadline = 0;
    /// Deadline for the current completion response.
    uint64_t completion_deadline = 0;
    /// Current generated virtual document path.
    godot::String uri;
    /// Current canonical project root URI.
    godot::String root_uri;
    /// Absolute project directory sent alongside the URI for LSP compatibility.
    godot::String root_path;
    /// Current generated document text.
    godot::String source;
    /// Version of the current document buffer.
    int64_t revision = 0;
    /// Last version sent to the language server.
    int64_t published = 0;
    /// Earliest time to send a new buffer revision after ordinary typing.
    uint64_t publish_at = 0;
    /// Most recent completion requested by the editor.
    std::optional<Completion> completion;
    /// Document versions awaiting diagnostics, in order on the dedicated peer.
    std::deque<int64_t> awaiting_diagnostics;

    /// Queue a complete JSON-RPC frame for nonblocking transmission.
    bool send(const char *method, const godot::Dictionary &parameters = {}, int64_t id = 0);
    /// Move buffered bytes to and from the peer.
    bool exchange();
    /// Dispatch all complete protocol frames currently waiting in the buffer.
    bool receive();
    /// Process a response or diagnostic notification associated with this client.
    void dispatch(const godot::Dictionary &message);
    /// Open or update the unsaved server document with the current source.
    void publish();
    /// Reset transport and report a useful failure once to the editor panel.
    void fail(const godot::String &message);

public:
    /// Completion rows from the server for the most recent matching cursor request.
    std::function<void(const godot::Array &)> on_completion;
    /// Diagnostics for the current generated source.
    std::function<void(const godot::Array &)> on_diagnostics;
    /// Inform the editor that the local language server is unavailable.
    std::function<void(const godot::String &)> on_unavailable;

    /// Open a unique virtual GDScript document, without writing any file into the project.
    void open(const godot::String &generated_source);
    /// Replace the source and invalidate outstanding requests bound to an old revision.
    void update(const godot::String &generated_source);
    /// Request completion at a position in the current generated source.
    void complete(int32_t line, int32_t column);
    /// Progress the connection and queued requests without blocking the editor.
    void poll();
    /// Release the virtual document and its dedicated connection.
    void close();
};

}

#endif
