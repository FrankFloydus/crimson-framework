// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#pragma once

#include <godot_cpp/classes/ref.hpp>
#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/core/error_macros.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/typed_array.hpp>

#include <cstdint>
#include <memory>

namespace crimson::steam {

class SteamRuntime;
struct SteamClientScope;

// A Steam SDK diagnostic with a safe description of its severity.
class SteamDiagnosticMessage : public godot::RefCounted {
    GDCLASS(SteamDiagnosticMessage, godot::RefCounted)

public:
    // Return the severity code reported by Steam.
    int64_t get_severity() const;
    // Return the fixed description selected for the severity.
    godot::String get_text() const;

protected:
    // Register the read-only severity and text properties with Godot.
    static void _bind_methods();

private:
    friend class SteamDiagnostics;

    // SDK severity code as a Godot integer.
    int64_t severity = 0;
    // Fixed framework text that can be displayed or logged.
    godot::String text;
};

// An immutable snapshot of a diagnostics drain.
class SteamDiagnosticBatch : public godot::RefCounted {
    GDCLASS(SteamDiagnosticBatch, godot::RefCounted)

public:
    // Initialize an empty snapshot with a read-only array.
    SteamDiagnosticBatch();
    // Return the read-only messages in arrival order.
    godot::TypedArray<SteamDiagnosticMessage> get_messages() const;
    // Return the number of callbacks dropped after the queue filled.
    int64_t get_dropped_count() const;

protected:
    // Register the read-only batch properties with Godot.
    static void _bind_methods();

private:
    friend class SteamDiagnostics;

    // Frozen typed array, detached from ongoing capture.
    godot::TypedArray<SteamDiagnosticMessage> messages;
    // Overflow since capture started or since the preceding drain.
    int64_t dropped_count = 0;
};

// Main-thread control of the shared Steam SDK warning hook.
class SteamDiagnostics : public godot::RefCounted {
    GDCLASS(SteamDiagnostics, godot::RefCounted)

public:
    // Claim capture for this client start, or report contention or unavailable Steam.
    godot::Error start();
    // Drain the owner's bounded queue or return an empty snapshot.
    godot::Ref<SteamDiagnosticBatch> drain();
    // Release this service's hook and discard its pending batch.
    void stop();

protected:
    // Register capture and drain operations with Godot.
    static void _bind_methods();

private:
    friend class SteamClient;

    // Observe the Steam runtime without extending its lifetime.
    std::weak_ptr<SteamRuntime> runtime;
    // Observe the exact client start that created this service.
    std::weak_ptr<SteamClientScope> client_scope;

    // Associate a fresh service with its originating runtime and scope.
    void attach(const std::shared_ptr<SteamRuntime> &active, const std::shared_ptr<SteamClientScope> &scope);
    // Release owned capture even after the client scope is retired.
    void detach();
};

} // namespace crimson::steam
