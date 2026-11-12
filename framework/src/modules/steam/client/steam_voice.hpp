// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#pragma once

#include "modules/steam/client/steam_result.hpp"

#include <godot_cpp/variant/packed_byte_array.hpp>

#include <memory>

namespace crimson::steam {

class SteamRuntime;
struct SteamClientScope;

// Immutable encoded audio chunk returned by Steam microphone capture.
class SteamVoiceFrame : public godot::RefCounted {
    GDCLASS(SteamVoiceFrame, godot::RefCounted)

public:
    // Return the opaque compressed chunk for an authorized voice route.
    godot::PackedByteArray get_bytes() const;

protected:
    // Register the read-only encoded chunk.
    static void _bind_methods();

private:
    friend class SteamVoiceFrameResult;

    // Detached bytes copied from the bounded native capture buffer.
    godot::PackedByteArray bytes;
};

// Immutable signed 16-bit mono audio for the game's playback path.
class SteamVoicePcm : public godot::RefCounted {
    GDCLASS(SteamVoicePcm, godot::RefCounted)

public:
    // Return the decoded little-endian samples.
    godot::PackedByteArray get_samples() const;
    // Return the decoded sample rate in Hz.
    int64_t get_sample_rate_hz() const;

protected:
    // Register the read-only PCM data and format.
    static void _bind_methods();

private:
    friend class SteamVoicePcmResult;

    // Detached signed 16-bit mono samples in little-endian order.
    godot::PackedByteArray samples;
    // Codec output sample rate.
    int64_t sample_rate_hz = 0;
};

// Outcome of polling one encoded capture frame.
class SteamVoiceFrameResult : public SteamResult {
    GDCLASS(SteamVoiceFrameResult, SteamResult)

public:
    // Return the bounded captured frame on success.
    godot::Ref<SteamVoiceFrame> get_value() const;

protected:
    // Register the typed capture result.
    static void _bind_methods();

private:
    friend class SteamVoiceCapture;

    // Build a terminal outcome with a copied frame only on success.
    static godot::Ref<SteamVoiceFrameResult> create(godot::Error error,
            const godot::PackedByteArray &bytes = {});

    // Frame retained when Steam returned compressed audio.
    godot::Ref<SteamVoiceFrame> value;
};

// Outcome of decoding one encoded Steam voice frame.
class SteamVoicePcmResult : public SteamResult {
    GDCLASS(SteamVoicePcmResult, SteamResult)

public:
    // Return the bounded decoded audio on success.
    godot::Ref<SteamVoicePcm> get_value() const;

protected:
    // Register the typed decoder result.
    static void _bind_methods();

private:
    friend class SteamVoice;

    // Build a terminal outcome with copied PCM only on success.
    static godot::Ref<SteamVoicePcmResult> create(godot::Error error,
            const godot::PackedByteArray &samples = {}, int64_t sample_rate_hz = 0);

    // Decoded audio retained when Steam decoded the supplied frame.
    godot::Ref<SteamVoicePcm> value;
};

// Explicit process-exclusive microphone capture bound to one client scope.
class SteamVoiceCapture : public godot::RefCounted {
    GDCLASS(SteamVoiceCapture, godot::RefCounted)

public:
    // Request microphone capture for this scope after player opt-in.
    godot::Error start();
    // Request stop and allow the exact owner to drain final captured frames.
    godot::Error stop();
    // Poll the bounded Steam capture buffer for this exact owner.
    godot::Ref<SteamVoiceFrameResult> read_frame();
    // Request native stop and make this exact owner's final data unreadable.
    godot::Error abandon();

protected:
    // Register push-to-talk capture controls.
    static void _bind_methods();

private:
    friend class SteamVoice;

    // Bind this handle to an exact client start.
    void attach(const std::shared_ptr<SteamRuntime> &active, const std::shared_ptr<SteamClientScope> &scope);
    // Retire this owner without delivering the microphone tail.
    void detach();
    // Resolve the scope only on the Steam main thread.
    std::shared_ptr<SteamRuntime> active_runtime() const;
    // Discard any native tail left by an expired recording or a departed capture owner.
    void tick();

    // Process Steam runtime borrowed by the exact client start.
    std::weak_ptr<SteamRuntime> runtime;
    // Exact client scope invalidated before teardown.
    std::weak_ptr<SteamClientScope> client_scope;
};

// Scoped Steam voice codec and opt-in microphone access.
class SteamVoice : public godot::RefCounted {
    GDCLASS(SteamVoice, godot::RefCounted)

public:
    // Maximum compressed frame accepted by decoding and the game voice lane.
    static constexpr int64_t MAX_FRAME_BYTES = 8192;

    // Resolve the scope's exclusive capture controls without recording.
    godot::Ref<SteamVoiceCapture> capture() const;
    // Resolve a distinct internal microphone identity for GameVoice.
    godot::Ref<SteamVoiceCapture> capture_for_game();
    // Decode a bounded chunk. A zero rate uses Steam's recommended sample rate.
    godot::Ref<SteamVoicePcmResult> decode(const godot::PackedByteArray &bytes, int64_t sample_rate_hz = 0);

protected:
    // Register the typed voice surface.
    static void _bind_methods();

private:
    friend class SteamClient;

    // Create the capture handle for this client start without acquiring the microphone.
    void attach(const std::shared_ptr<SteamRuntime> &active, const std::shared_ptr<SteamClientScope> &scope);
    // Stop owned recording and invalidate retained voice handles.
    void detach();
    // Poll only expired capture data requiring bounded native cleanup.
    void tick();

    // Capture handle that identifies this exact scope to the process lease.
    godot::Ref<SteamVoiceCapture> capture_handle;
    // Separate GameVoice owner under the same client scope and process lease.
    godot::Ref<SteamVoiceCapture> game_capture_handle;
};

} // namespace crimson::steam
