// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#include "modules/steam/client/steam_voice.hpp"

#include "modules/steam/client/steam_client_scope.hpp"

#include <godot_cpp/core/class_db.hpp>

#ifdef CRIMSON_WITH_STEAM
#include "modules/steam/runtime/steam_runtime.hpp"

#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/time.hpp>
#include <godot_cpp/core/object_id.hpp>

#include <array>
#include <cstring>
#include <limits>
#endif

namespace crimson::steam {
using namespace godot;

namespace {

// Steam voice output format bounds.
constexpr int64_t minimum_sample_rate = 11025;
constexpr int64_t maximum_sample_rate = 48000;

#ifdef CRIMSON_WITH_STEAM
// An oversized native packet is discarded instead of being divided into invalid codec frames.
constexpr size_t native_capture_buffer = 32 * 1024;
// Bound native decompression allocations for unknown encoded frames.
constexpr uint32_t maximum_pcm_bytes = 128 * 1024;
// Cap exposure of post-release audio. Any remainder is discarded by the process lease.
constexpr uint64_t deliver_tail_ms = 300;

// Tracks native microphone ownership across client scopes sharing the exact runtime.
struct VoiceLease {
    enum class Phase { Idle, Recording, Tail, Discard };

    // Runtime generation whose ISteamUser owns this capture.
    std::weak_ptr<SteamRuntime> runtime;
    // Capture handle allowed to read encoded audio in the current lease.
    ObjectID owner;
    // Native recording or shutdown progress.
    Phase phase = Phase::Idle;
    // Last monotonic millisecond when the previous owner may receive final audio.
    uint64_t deadline = 0;
    // Limit cleanup to one native poll per Godot frame across all scopes.
    uint64_t last_discard_frame = (std::numeric_limits<uint64_t>::max)();
};

// Main-thread-only microphone state. Its weak generation does not extend Steam's lifetime.
VoiceLease voice_lease;

// Resolve Steam's client User interface without including Steam in the Godot-facing header.
ISteamUser *get_user(const std::shared_ptr<SteamRuntime> &active) {
    const auto get = active ? active->symbol<decltype(&SteamAPI_SteamUser_v023)>("SteamAPI_SteamUser_v023") : nullptr;
    return get ? get() : nullptr;
}

// Select the native recording state for this runtime generation.
VoiceLease &lease_for(const std::shared_ptr<SteamRuntime> &active) {
    if (voice_lease.runtime.lock() != active) {
        voice_lease = {};
        voice_lease.runtime = active;
    }
    return voice_lease;
}

// Relinquish the capture owner's access to any remaining native audio.
void discard_tail(VoiceLease &lease) {
    lease.owner = ObjectID();
    lease.phase = VoiceLease::Phase::Discard;
    lease.deadline = 0;
}

// Read compressed audio only, leaving deprecated uncompressed options disabled.
EVoiceResult read_native(ISteamUser *user, decltype(&SteamAPI_ISteamUser_GetVoice) read,
        std::array<uint8_t, native_capture_buffer> &buffer, uint32_t &written) {
    return read(user, true, buffer.data(), static_cast<uint32_t>(buffer.size()), &written,
            false, nullptr, 0, nullptr, 0);
}

// Translate documented Steam voice outcomes into bounded Godot results.
Error voice_error(EVoiceResult result) {
    switch (result) {
        case k_EVoiceResultOK: return OK;
        case k_EVoiceResultNotInitialized:
        case k_EVoiceResultNotRecording:
        case k_EVoiceResultNoData:
        case k_EVoiceResultUnsupportedCodec:
        case k_EVoiceResultReceiverOutOfDate: return ERR_UNAVAILABLE;
        case k_EVoiceResultBufferTooSmall: return ERR_OUT_OF_MEMORY;
        case k_EVoiceResultDataCorrupted: return ERR_FILE_CORRUPT;
        case k_EVoiceResultRestricted: return ERR_UNAUTHORIZED;
        default: return FAILED;
    }
}

// Copy only a successful native capture's exact byte range.
PackedByteArray copy_bytes(const uint8_t *data, uint32_t size) {
    PackedByteArray copied;
    if (size != 0 && copied.resize(size) == OK) {
        std::memcpy(copied.ptrw(), data, size);
    }
    return copied;
}
#endif

} // namespace

void SteamVoiceFrame::_bind_methods() {
    ClassDB::bind_method(D_METHOD("get_bytes"), &SteamVoiceFrame::get_bytes);
    ADD_PROPERTY(PropertyInfo(Variant::PACKED_BYTE_ARRAY, "bytes", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_READ_ONLY),
            "", "get_bytes");
}

PackedByteArray SteamVoiceFrame::get_bytes() const { return bytes; }

void SteamVoicePcm::_bind_methods() {
    ClassDB::bind_method(D_METHOD("get_samples"), &SteamVoicePcm::get_samples);
    ClassDB::bind_method(D_METHOD("get_sample_rate_hz"), &SteamVoicePcm::get_sample_rate_hz);
    ADD_PROPERTY(PropertyInfo(Variant::PACKED_BYTE_ARRAY, "samples", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_READ_ONLY),
            "", "get_samples");
    ADD_PROPERTY(PropertyInfo(Variant::INT, "sample_rate_hz", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_READ_ONLY),
            "", "get_sample_rate_hz");
}

PackedByteArray SteamVoicePcm::get_samples() const { return samples; }
int64_t SteamVoicePcm::get_sample_rate_hz() const { return sample_rate_hz; }

void SteamVoiceFrameResult::_bind_methods() {
    ClassDB::bind_method(D_METHOD("get_value"), &SteamVoiceFrameResult::get_value);
    ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "value", PROPERTY_HINT_RESOURCE_TYPE, "SteamVoiceFrame",
                         PROPERTY_USAGE_READ_ONLY), "", "get_value");
}

Ref<SteamVoiceFrame> SteamVoiceFrameResult::get_value() const { return value; }

Ref<SteamVoiceFrameResult> SteamVoiceFrameResult::create(Error error, const PackedByteArray &bytes) {
    Ref<SteamVoiceFrameResult> result;
    result.instantiate();
    result->set_outcome(error);
    if (error == OK) {
        result->value.instantiate();
        result->value->bytes = bytes;
    }
    return result;
}

void SteamVoicePcmResult::_bind_methods() {
    ClassDB::bind_method(D_METHOD("get_value"), &SteamVoicePcmResult::get_value);
    ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "value", PROPERTY_HINT_RESOURCE_TYPE, "SteamVoicePcm",
                         PROPERTY_USAGE_READ_ONLY), "", "get_value");
}

Ref<SteamVoicePcm> SteamVoicePcmResult::get_value() const { return value; }

Ref<SteamVoicePcmResult> SteamVoicePcmResult::create(Error error, const PackedByteArray &samples, int64_t rate) {
    Ref<SteamVoicePcmResult> result;
    result.instantiate();
    result->set_outcome(error);
    if (error == OK) {
        result->value.instantiate();
        result->value->samples = samples;
        result->value->sample_rate_hz = rate;
    }
    return result;
}

void SteamVoiceCapture::_bind_methods() {
    ClassDB::bind_method(D_METHOD("start"), &SteamVoiceCapture::start);
    ClassDB::bind_method(D_METHOD("stop"), &SteamVoiceCapture::stop);
    ClassDB::bind_method(D_METHOD("read_frame"), &SteamVoiceCapture::read_frame);
}

void SteamVoiceCapture::attach(const std::shared_ptr<SteamRuntime> &active,
        const std::shared_ptr<SteamClientScope> &scope) {
    runtime = active;
    client_scope = scope;
}

void SteamVoiceCapture::detach() {
#ifdef CRIMSON_WITH_STEAM
    if (SteamRuntime::on_main_thread()) {
        const auto active = runtime.lock();
        if (active) {
            auto &lease = lease_for(active);
            if (lease.owner == ObjectID(get_instance_id())) {
                if (lease.phase == VoiceLease::Phase::Recording) {
                    const auto stop = active->symbol<decltype(&SteamAPI_ISteamUser_StopVoiceRecording)>(
                            "SteamAPI_ISteamUser_StopVoiceRecording");
                    ISteamUser *user = get_user(active);
                    if (stop && user) {
                        stop(user);
                    }
                }
                discard_tail(lease);
            }
        }
    }
#endif
    client_scope.reset();
    runtime.reset();
}

std::shared_ptr<SteamRuntime> SteamVoiceCapture::active_runtime() const {
#ifdef CRIMSON_WITH_STEAM
    if (SteamRuntime::on_main_thread()) {
        const auto scope = client_scope.lock();
        if (scope && scope->active) {
            return runtime.lock();
        }
    }
#endif
    return {};
}

void SteamVoiceCapture::tick() {
#ifdef CRIMSON_WITH_STEAM
    const auto active = active_runtime();
    if (!active) {
        return;
    }
    auto &lease = lease_for(active);
    if (lease.phase == VoiceLease::Phase::Tail && Time::get_singleton()->get_ticks_msec() >= lease.deadline) {
        discard_tail(lease);
    }
    const uint64_t frame = Engine::get_singleton()->get_process_frames();
    if (lease.phase != VoiceLease::Phase::Discard || frame == lease.last_discard_frame) {
        return;
    }
    lease.last_discard_frame = frame;
    ISteamUser *user = get_user(active);
    const auto read = active->symbol<decltype(&SteamAPI_ISteamUser_GetVoice)>("SteamAPI_ISteamUser_GetVoice");
    if (read && user) {
        std::array<uint8_t, native_capture_buffer> discarded;
        uint32_t written = 0;
        if (read_native(user, read, discarded, written) == k_EVoiceResultNotRecording) {
            lease.phase = VoiceLease::Phase::Idle;
        }
    }
#endif
}

Error SteamVoiceCapture::start() {
#ifdef CRIMSON_WITH_STEAM
    const auto active = active_runtime();
    ISteamUser *user = get_user(active);
    if (!user) {
        return ERR_UNAVAILABLE;
    }
    const auto start = active->symbol<decltype(&SteamAPI_ISteamUser_StartVoiceRecording)>(
            "SteamAPI_ISteamUser_StartVoiceRecording");
    const auto stop = active->symbol<decltype(&SteamAPI_ISteamUser_StopVoiceRecording)>(
            "SteamAPI_ISteamUser_StopVoiceRecording");
    const auto read = active->symbol<decltype(&SteamAPI_ISteamUser_GetVoice)>("SteamAPI_ISteamUser_GetVoice");
    if (!start || !stop || !read) {
        return ERR_UNAVAILABLE;
    }
    tick();
    auto &lease = lease_for(active);
    const ObjectID id(get_instance_id());
    if (lease.phase == VoiceLease::Phase::Recording && lease.owner == id) {
        return OK;
    }
    if (lease.phase != VoiceLease::Phase::Idle) {
        return ERR_BUSY;
    }
    lease.owner = id;
    lease.phase = VoiceLease::Phase::Recording;
    start(user);
    return OK;
#else
    return ERR_UNAVAILABLE;
#endif
}

Error SteamVoiceCapture::stop() {
#ifdef CRIMSON_WITH_STEAM
    const auto active = active_runtime();
    ISteamUser *user = get_user(active);
    if (!user) {
        return ERR_UNAVAILABLE;
    }
    auto &lease = lease_for(active);
    if (lease.owner != ObjectID(get_instance_id())) {
        return ERR_UNAVAILABLE;
    }
    if (lease.phase == VoiceLease::Phase::Tail) {
        return OK;
    }
    const auto stop = active->symbol<decltype(&SteamAPI_ISteamUser_StopVoiceRecording)>(
            "SteamAPI_ISteamUser_StopVoiceRecording");
    if (!stop || lease.phase != VoiceLease::Phase::Recording) {
        return ERR_UNAVAILABLE;
    }
    stop(user);
    lease.phase = VoiceLease::Phase::Tail;
    lease.deadline = Time::get_singleton()->get_ticks_msec() + deliver_tail_ms;
    return OK;
#else
    return ERR_UNAVAILABLE;
#endif
}

Error SteamVoiceCapture::abandon() {
    const Error stopped = stop();
#ifdef CRIMSON_WITH_STEAM
    if (stopped == OK) {
        const auto active = active_runtime();
        if (active) {
            auto &lease = lease_for(active);
            if (lease.owner == ObjectID(get_instance_id())) {
                discard_tail(lease);
            }
        }
    }
#endif
    return stopped;
}

Ref<SteamVoiceFrameResult> SteamVoiceCapture::read_frame() {
#ifdef CRIMSON_WITH_STEAM
    const auto active = active_runtime();
    ISteamUser *user = get_user(active);
    if (!user) {
        return SteamVoiceFrameResult::create(ERR_UNAVAILABLE);
    }
    tick();
    auto &lease = lease_for(active);
    if (lease.owner != ObjectID(get_instance_id()) ||
            (lease.phase != VoiceLease::Phase::Recording && lease.phase != VoiceLease::Phase::Tail)) {
        return SteamVoiceFrameResult::create(ERR_UNAVAILABLE);
    }
    const auto read = active->symbol<decltype(&SteamAPI_ISteamUser_GetVoice)>("SteamAPI_ISteamUser_GetVoice");
    if (!read) {
        return SteamVoiceFrameResult::create(ERR_UNAVAILABLE);
    }

    std::array<uint8_t, native_capture_buffer> buffer;
    uint32_t written = 0;
    const EVoiceResult status = read_native(user, read, buffer, written);
    if (status == k_EVoiceResultNotRecording) {
        lease.owner = ObjectID();
        lease.phase = VoiceLease::Phase::Idle;
        lease.deadline = 0;
    }
    if (status != k_EVoiceResultOK || written == 0) {
        return SteamVoiceFrameResult::create(status == k_EVoiceResultOK ? ERR_UNAVAILABLE : voice_error(status));
    }
    if (written > SteamVoice::MAX_FRAME_BYTES) {
        return SteamVoiceFrameResult::create(ERR_INVALID_DATA);
    }
    const auto bytes = copy_bytes(buffer.data(), written);
    return bytes.is_empty() ? SteamVoiceFrameResult::create(ERR_OUT_OF_MEMORY) :
                             SteamVoiceFrameResult::create(OK, bytes);
#else
    return SteamVoiceFrameResult::create(ERR_UNAVAILABLE);
#endif
}

void SteamVoice::_bind_methods() {
    ClassDB::bind_method(D_METHOD("capture"), &SteamVoice::capture);
    ClassDB::bind_method(D_METHOD("decode", "bytes", "sample_rate_hz"), &SteamVoice::decode, DEFVAL(0));
}

void SteamVoice::attach(const std::shared_ptr<SteamRuntime> &active, const std::shared_ptr<SteamClientScope> &scope) {
    capture_handle.instantiate();
    capture_handle->attach(active, scope);
}

void SteamVoice::detach() {
    if (capture_handle.is_valid()) {
        capture_handle->detach();
    }
    if (game_capture_handle.is_valid()) {
        game_capture_handle->detach();
    }
}

void SteamVoice::tick() {
    if (capture_handle.is_valid()) {
        capture_handle->tick();
    }
}

Ref<SteamVoiceCapture> SteamVoice::capture() const { return capture_handle; }

Ref<SteamVoiceCapture> SteamVoice::capture_for_game() {
    if (capture_handle.is_null() || !capture_handle->active_runtime()) {
        return {};
    }
    if (game_capture_handle.is_null()) {
        game_capture_handle.instantiate();
        game_capture_handle->attach(capture_handle->runtime.lock(), capture_handle->client_scope.lock());
    }
    return game_capture_handle;
}

Ref<SteamVoicePcmResult> SteamVoice::decode(const PackedByteArray &bytes, int64_t sample_rate_hz) {
    if (bytes.is_empty() || bytes.size() > MAX_FRAME_BYTES ||
            (sample_rate_hz != 0 && (sample_rate_hz < minimum_sample_rate || sample_rate_hz > maximum_sample_rate))) {
        return SteamVoicePcmResult::create(ERR_INVALID_PARAMETER);
    }
#ifdef CRIMSON_WITH_STEAM
    const auto active = capture_handle.is_valid() ? capture_handle->active_runtime() : nullptr;
    ISteamUser *user = get_user(active);
    if (!user) {
        return SteamVoicePcmResult::create(ERR_UNAVAILABLE);
    }
    const auto decompress = active->symbol<decltype(&SteamAPI_ISteamUser_DecompressVoice)>(
            "SteamAPI_ISteamUser_DecompressVoice");
    if (!decompress) {
        return SteamVoicePcmResult::create(ERR_UNAVAILABLE);
    }
    if (sample_rate_hz == 0) {
        const auto optimal = active->symbol<decltype(&SteamAPI_ISteamUser_GetVoiceOptimalSampleRate)>(
                "SteamAPI_ISteamUser_GetVoiceOptimalSampleRate");
        sample_rate_hz = optimal ? optimal(user) : 0;
        if (sample_rate_hz < minimum_sample_rate || sample_rate_hz > maximum_sample_rate) {
            return SteamVoicePcmResult::create(ERR_UNAVAILABLE);
        }
    }

    PackedByteArray samples;
    if (samples.resize(maximum_pcm_bytes) != OK) {
        return SteamVoicePcmResult::create(ERR_OUT_OF_MEMORY);
    }
    uint32_t written = 0;
    const auto status = decompress(user, bytes.ptr(), static_cast<uint32_t>(bytes.size()), samples.ptrw(),
            maximum_pcm_bytes, &written, static_cast<uint32_t>(sample_rate_hz));
    if (status != k_EVoiceResultOK) {
        return SteamVoicePcmResult::create(voice_error(status));
    }
    if (written == 0 || written > maximum_pcm_bytes || written % 2 != 0 || samples.resize(written) != OK) {
        return SteamVoicePcmResult::create(ERR_INVALID_DATA);
    }
    return SteamVoicePcmResult::create(OK, samples, sample_rate_hz);
#else
    return SteamVoicePcmResult::create(ERR_UNAVAILABLE);
#endif
}

} // namespace crimson::steam
