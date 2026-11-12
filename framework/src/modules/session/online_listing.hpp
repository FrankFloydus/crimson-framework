// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#pragma once

#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/string_name.hpp>

namespace crimson::session {

// Detached provider-owned locator for an online session.
class OnlineListing : public godot::RefCounted {
    GDCLASS(OnlineListing, godot::RefCounted)

public:
    // Return the provider responsible for resolving this locator.
    godot::StringName get_provider() const;
    // Return the opaque directory identifier for fresh resolution.
    godot::String get_id() const;

protected:
    // Register the immutable provider and identifier.
    static void _bind_methods();
    // Populate a locator from a provider-owned snapshot before publishing it.
    void set_locator(const godot::StringName &provider, const godot::String &id);

private:
    // Provider key assigned when the snapshot was produced.
    godot::StringName provider;
    // Provider-owned identifier, independent of the current gameplay endpoint.
    godot::String id;
};

} // namespace crimson::session
