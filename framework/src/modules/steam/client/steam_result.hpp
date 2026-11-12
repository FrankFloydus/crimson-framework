// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#pragma once

#include <godot_cpp/classes/ref.hpp>
#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/variant/string.hpp>

namespace crimson::steam {

// Shared status fields for completed Steam actions. Derived classes bind typed payloads.
class SteamResult : public godot::RefCounted {
    GDCLASS(SteamResult, godot::RefCounted)

public:
    // Create a settled action result without a payload.
    static godot::Ref<SteamResult> from_error(godot::Error error, const godot::String &message = {});
    // Report whether this action completed successfully under its own contract.
    bool is_ok() const;
    // Return the Godot error describing the terminal outcome.
    godot::Error get_error() const;
    // Return an optional diagnostic suitable for presentation or logs.
    godot::String get_message() const;

protected:
    // Register read-only status properties for Godot consumers.
    static void _bind_methods();
    // Initialize status before publishing a completed native result.
    void set_outcome(godot::Error error, const godot::String &message = {});

private:
    // Terminal status. A script-created empty result is unconfigured.
    godot::Error error = godot::ERR_UNCONFIGURED;
    // Safe contextual diagnostic for the terminal status.
    godot::String message;
};

// Completed Steam action with a boolean payload.
class SteamBoolResult : public SteamResult {
    GDCLASS(SteamBoolResult, SteamResult)

public:
    // Create a settled result. An unsuccessful query has no true payload.
    static godot::Ref<SteamBoolResult> create(godot::Error error, bool value = false,
            const godot::String &message = {});
    // Return the confirmed boolean value.
    bool get_value() const;

protected:
    // Register the read-only boolean payload.
    static void _bind_methods();

private:
    // Value returned by the operation on success.
    bool value = false;
};

// Completed Steam action with a text payload.
class SteamStringResult : public SteamResult {
    GDCLASS(SteamStringResult, SteamResult)

public:
    // Create a settled result with a payload on success.
    static godot::Ref<SteamStringResult> create(godot::Error error, const godot::String &value = {},
            const godot::String &message = {});
    // Return the text provided by Steam on success.
    godot::String get_value() const;

protected:
    // Register the read-only text payload.
    static void _bind_methods();

private:
    // Steam response content, which can include a temporary URL.
    godot::String value;
};

} // namespace crimson::steam
