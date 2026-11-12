// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#pragma once

#include "modules/steam/client/steam_result.hpp"

#include <godot_cpp/classes/image.hpp>
#include <godot_cpp/core/object.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>
#include <godot_cpp/variant/typed_array.hpp>
#include <godot_cpp/variant/vector2i.hpp>

#include <cstdint>
#include <map>
#include <memory>
#include <vector>

namespace crimson::steam {

class SteamRuntime;
struct SteamClientScope;
class SteamScreenshots;

// Describes a confirmed Steam screenshot and the immediate outcomes of optional metadata requests.
class SteamScreenshot : public godot::RefCounted {
    GDCLASS(SteamScreenshot, godot::RefCounted)

public:
    // Return true when every requested annotation was accepted by Steam.
    bool get_annotations_accepted() const;
    // Return the read-only list of annotation failures, in location, user, item order.
    godot::TypedArray<int64_t> get_annotation_errors() const;

protected:
    // Register read-only screenshot metadata for Godot.
    static void _bind_methods();

private:
    friend class SteamScreenshots;

    // Immutable once this snapshot is returned to script.
    godot::TypedArray<int64_t> errors;
};

// Terminal screenshot submission status with a value only after an exact successful save callback.
class SteamScreenshotResult : public SteamResult {
    GDCLASS(SteamScreenshotResult, SteamResult)

public:
    // Return the confirmed screenshot or null after a failed observation.
    godot::Ref<SteamScreenshot> get_value() const;

protected:
    // Register the typed read-only payload.
    static void _bind_methods();

private:
    friend class SteamScreenshotOperation;

    // Create a result with a validated outcome before publishing it to script.
    static godot::Ref<SteamScreenshotResult> create(godot::Error error, const godot::String &message,
            const godot::Ref<SteamScreenshot> &screenshot);

    // Snapshot returned after Steam confirms the save.
    godot::Ref<SteamScreenshot> value;
};

// Retains a submitted image's observation independently of a waiting caller.
class SteamScreenshotOperation : public godot::RefCounted {
    GDCLASS(SteamScreenshotOperation, godot::RefCounted)

public:
    // Return whether an exact callback, deadline, or scope exit settled the observation.
    bool is_finished() const;
    // Return the stored result or null while pending.
    godot::Ref<SteamScreenshotResult> result() const;
    // Return the result now or a signal whose single argument is that result.
    godot::Variant wait();
    // Cancel local observation and skip remaining annotations while Steam may still save the image.
    godot::Error cancel();

protected:
    // Register typed completion observation and cancellation.
    static void _bind_methods();

private:
    friend class SteamScreenshots;
    friend class SteamScreenshotRequest;

    // Settle once and drop the owner before emitting the completion signal.
    void finish(godot::Error error, const godot::String &message,
            const godot::Ref<SteamScreenshot> &screenshot = {});

    // Exact facade which issued the native write, if still pending.
    godot::ObjectID owner;
    // Process screenshot handle used only to correlate the native callback.
    uint32_t handle = 0;
    // Terminal result, set before observers run.
    godot::Ref<SteamScreenshotResult> outcome;
};

// Immutable, branchable draft for one photo. Steam receives it only at submission.
class SteamScreenshotRequest : public godot::RefCounted {
    GDCLASS(SteamScreenshotRequest, godot::RefCounted)

public:
    // Return a copy with the supplied location. Empty text removes the location.
    godot::Ref<SteamScreenshotRequest> at(const godot::String &location) const;
    // Return a copy with an individual SteamID64 tag.
    godot::Ref<SteamScreenshotRequest> tag(const godot::String &steam_id) const;
    // Return a copy with a decimal Workshop published-file ID tag.
    godot::Ref<SteamScreenshotRequest> tag_item(const godot::String &published_id) const;
    // Submit once and return its completion signal or immediate typed result.
    godot::Variant submit() const;
    // Submit once and return a retained handle for polling or local cancellation.
    godot::Ref<SteamScreenshotOperation> send() const;

protected:
    // Register immutable draft edits and the two terminal forms.
    static void _bind_methods();

private:
    friend class SteamScreenshots;

    // Copy draft metadata and share the immutable pixel buffer through copy-on-write.
    godot::Ref<SteamScreenshotRequest> branch() const;

    // Exact owner facade, never retained by the draft.
    godot::ObjectID owner;
    // Bounded packed RGB8 snapshot captured at from_image.
    godot::PackedByteArray pixels;
    // Pixel dimensions of the snapshot.
    godot::Vector2i size;
    // Optional text location encoded on submission.
    godot::String location;
    // Individual SteamID64s requested as user tags.
    godot::PackedStringArray users;
    // Published-file IDs requested as Workshop tags.
    godot::PackedStringArray items;
    // Draft validation error when the request cannot be submitted.
    godot::Error draft_error = godot::OK;
};

// Submits Steam screenshots and observes saved images within an exact client scope.
class SteamScreenshots : public godot::RefCounted {
    GDCLASS(SteamScreenshots, godot::RefCounted)

public:
    // Return whether the scoped SDK interface can observe and submit screenshots.
    bool is_available() const;
    // Ask Steam's live overlay to take its ordinary unannotated screenshot.
    godot::Error capture() const;
    // Snapshot an uncompressed image and create a draft with no annotations.
    godot::Ref<SteamScreenshotRequest> from_image(const godot::Ref<godot::Image> &image);

    // Observe the screenshot-ready callback for the original live client start.
    void attach(const std::shared_ptr<SteamRuntime> &active, const std::shared_ptr<SteamClientScope> &scope);
    // Release callback routing and settle every observation before returning to the game.
    void detach();
    // Settle expired exact-handle observations on the scene thread.
    void tick();

protected:
    // Register screenshot queries, submissions, and observation signals.
    static void _bind_methods();

private:
    friend class SteamScreenshotOperation;
    friend class SteamScreenshotRequest;

    // Metadata and operation retained until the corresponding Steam handle settles.
    struct Pending {
        // Observer retained until its exact callback, cancellation, or timeout.
        godot::Ref<SteamScreenshotOperation> operation;
        // Validated UTF-8 location or empty when no location was requested.
        godot::CharString location;
        // Validated individual account IDs.
        std::vector<uint64_t> users;
        // Validated published-file IDs.
        std::vector<uint64_t> items;
        // Monotonic millisecond deadline for local observation.
        uint64_t deadline = 0;
    };

    // Resolve this facade's runtime on the main thread while its exact scope is live.
    std::shared_ptr<SteamRuntime> active_runtime() const;
    // Validate the current draft and start an exact asynchronous screenshot write.
    godot::Ref<SteamScreenshotOperation> send(const SteamScreenshotRequest &draft);
    // Drop a pending observation before settling it locally.
    godot::Error cancel(uint32_t handle);
    // Correlate the SDK result and emit a typed saved screenshot when successful.
    void on_ready(uint32_t handle, bool saved);

    // Runtime shared by active client starts without keeping a stopped runtime alive.
    std::weak_ptr<SteamRuntime> runtime;
    // Liveness of this facade's original start generation.
    std::weak_ptr<SteamClientScope> client_scope;
    // Opaque SteamRuntime dispatcher subscription identifier.
    uint64_t subscription = 0;
    // Exact Steam handles awaiting confirmation.
    std::map<uint32_t, Pending> pending;
};

} // namespace crimson::steam
