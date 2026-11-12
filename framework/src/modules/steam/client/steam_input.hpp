// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#pragma once

#include "modules/steam/client/steam_input_types.hpp"

#include <godot_cpp/core/object_id.hpp>
#include <godot_cpp/variant/string_name.hpp>

#include <cstdint>
#include <map>
#include <memory>
#include <utility>

namespace crimson::steam {

class SteamRuntime;
class SteamInput;
struct SteamClientScope;
class SteamInputSession;

// Reads and configures one Steam Input controller connection.
class SteamController : public godot::RefCounted {
    GDCLASS(SteamController, godot::RefCounted)

public:
    // Device families reported by the supported Steam Input interface.
    enum Type {
        TYPE_UNKNOWN = 0, // Steam did not identify a supported controller family.
        TYPE_STEAM_CONTROLLER = 1, // Steam Controller (2015).
        TYPE_XBOX_360 = 2, // Xbox 360 controller.
        TYPE_XBOX_ONE = 3, // Xbox One controller.
        TYPE_GENERIC_GAMEPAD = 4, // Generic gamepad.
        TYPE_PLAYSTATION_4 = 5, // PlayStation 4 controller.
        TYPE_APPLE_MFI = 6, // Apple MFi controller.
        TYPE_ANDROID = 7, // Android controller.
        TYPE_SWITCH_JOYCON_PAIR = 8, // Paired Nintendo Switch Joy-Con controllers.
        TYPE_SWITCH_JOYCON_SINGLE = 9, // One Nintendo Switch Joy-Con controller.
        TYPE_SWITCH_PRO = 10, // Nintendo Switch Pro Controller.
        TYPE_MOBILE_TOUCH = 11, // Steam Link virtual touch controller.
        TYPE_PLAYSTATION_3 = 12, // PlayStation 3 controller.
        TYPE_PLAYSTATION_5 = 13, // PlayStation 5 controller.
        TYPE_STEAM_DECK = 14, // Steam Deck controller.
        TYPE_STEAMOS_HANDHELD = 15, // SteamOS handheld controller.
        TYPE_SWITCH_2_PRO = 16, // Nintendo Switch 2 Pro Controller.
        TYPE_STEAM_CONTROLLER_2026 = 17, // Steam Controller (2026).
        TYPE_STEAM_FRAME_PAIR = 18, // Paired Steam Frame controllers.
    };

    // Size of the Steam-provided PNG used for a binding prompt.
    enum GlyphSize {
        SMALL = 0, // 32-pixel glyph.
        MEDIUM = 1, // 128-pixel glyph.
        LARGE = 2, // 256-pixel glyph.
    };

    // Report whether this view still represents the same connected Steam controller.
    bool is_connected() const;
    // Return Steam's device family or TYPE_UNKNOWN when unavailable.
    Type type() const;
    // Select a named action set from the loaded manifest for this controller.
    godot::Error use_action_set(const godot::StringName &name);
    // Add a named temporary action set layer to this controller.
    godot::Error activate_layer(const godot::StringName &name);
    // Remove a named temporary action set layer from this controller.
    godot::Error deactivate_layer(const godot::StringName &name);
    // Read the current digital state of a named action.
    godot::Ref<SteamDigitalInputResult> digital(const godot::StringName &action) const;
    // Read the current analog values and input source mode of a named action.
    godot::Ref<SteamAnalogInputResult> analog(const godot::StringName &action) const;
    // Load the first effective binding's PNG texture for a named action.
    godot::Ref<SteamTextureResult> glyph(const godot::StringName &action, GlyphSize size = MEDIUM) const;
    // Load the effective binding origins' PNG textures in Steam's reported order.
    godot::Ref<SteamTexturesResult> glyphs(const godot::StringName &action, GlyphSize size = MEDIUM) const;
    // Ask Steam to open its binding configuration panel for this controller.
    godot::Error open_binding_panel();
    // Submit normalized low-frequency and high-frequency vibration motor values.
    godot::Error vibrate(double low, double high);

protected:
    // Register the typed controller API and enum values with Godot.
    static void _bind_methods();

private:
    friend class SteamInput;

    // Select the native command for a set or layer action.
    enum class SetAction { Select, ActivateLayer, DeactivateLayer };

    // Resolve the facade and lease only for this exact connected view.
    godot::Ref<SteamInput> active_owner() const;
    // Dispatch a named action set or layer change after checking this connection.
    godot::Error change_action_set(const godot::StringName &name, SetAction action);
    // Resolve one or all active binding glyphs through this scope's texture cache.
    godot::Error read_glyphs(const godot::StringName &action, GlyphSize size,
            bool first_only, godot::TypedArray<godot::Texture2D> &textures) const;

    // Exact service that published this connection view.
    godot::ObjectID owner;
    // Opaque Steam SDK handle, private to the current native lease.
    uint64_t handle = 0;
    // Identity of this handle's exact connect event within the lease.
    uint64_t connection = 0;
    // Latest binding change this facade has observed for this connection.
    uint64_t binding_revision = 0;
};

// Leases Steam Input for one exact client start and publishes typed connection views.
class SteamInput : public godot::RefCounted {
    GDCLASS(SteamInput, godot::RefCounted)

public:
    // Acquire the process input interface and select an optional loose action manifest.
    godot::Error start(const godot::String &manifest_path = {});
    // Release this scope's lease and immediately invalidate its controller views.
    void stop();
    // Return the current connected controller views, or an empty array before start.
    godot::TypedArray<SteamController> controllers() const;

protected:
    // Register the typed lease API and controller event signals.
    static void _bind_methods();

private:
    friend class SteamClient;
    friend class SteamController;

    // Associate this facade with a new client start without activating Steam Input.
    void attach(const std::shared_ptr<SteamRuntime> &active, const std::shared_ptr<SteamClientScope> &scope);
    // Retire the client start and release any owned input lease.
    void detach();
    // Run the shared input update and reconcile this scope's views on the main thread.
    void tick();
    // Resolve the exact live client scope on the main thread.
    std::shared_ptr<SteamRuntime> active_runtime() const;
    // Verify that this scope still owns the supplied live connection view.
    bool owns_controller(const SteamController &controller) const;
    // Load and cache a Steam-selected glyph for the active lease.
    godot::Ref<godot::Texture2D> glyph_texture(int origin, int size);

    // Process runtime observed without extending the client scope.
    std::weak_ptr<SteamRuntime> runtime;
    // Exact client start that created this facade.
    std::weak_ptr<SteamClientScope> client_scope;
    // Shared native input lease retained only while this facade is started.
    std::shared_ptr<SteamInputSession> session;
    // Local views keyed by native handle and checked against connection identity.
    std::map<uint64_t, godot::Ref<SteamController>> views;
    // Scoped texture cache keyed by the native binding origin and glyph size.
    std::map<std::pair<int, int>, godot::Ref<godot::Texture2D>> glyph_cache;
    // Changes on start and stop to guard queued signal emissions against reentry.
    uint64_t lease_generation = 0;
};

} // namespace crimson::steam

VARIANT_ENUM_CAST(crimson::steam::SteamController::Type);
VARIANT_ENUM_CAST(crimson::steam::SteamController::GlyphSize);
