// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#pragma once

#include "controls.hpp"

#include <map>
#include <set>
#include <vector>

namespace crimson::input {

// Private scene driver for one local device and action scope.
class InputRouter : public godot::Node {
    GDCLASS(InputRouter, godot::Node)

    friend class Controls;

    // Track live device owners by weak identity.
    static std::set<godot::ObjectID> live_scopes;

    // Mutable state of one physical source and its immutable authored default.
    struct Binding {
        // Snapshot of the defaults, including slot metadata.
        godot::Ref<InputBinding> definition;
        // Effective physical event after scoped rebinding.
        godot::Ref<godot::InputEvent> event;
        // Ordered private modifier snapshots ready to evaluate without Godot array allocation.
        std::vector<godot::Ref<InputModifier>> modifiers;
        // Last admitted level of this source, before contribution scaling.
        float strength = 0.0f;
        // Physical level observed even when GUI or an inactive map blocks admission.
        float observed_strength = 0.0f;
        // Most recent admitted signed X and Y levels of a whole-stick source.
        godot::Vector2 stick_value;
        // Raw stick components used for neutral gating even while admission is blocked.
        godot::Vector2 observed_stick;
        // A source held across a map switch must return to neutral before admission.
        bool wait_for_neutral = false;
        // The raw event that reopened the gate is still neutral in the unhandled pass.
        bool neutral_sample = false;

        // Apply this binding's ordered transforms to a source value.
        godot::Vector2 modified(godot::Vector2 value) const;
        // Observe a signed stick axis, admitting new levels only when permitted.
        void update_stick(godot::JoyAxis axis, float value, bool raw, bool eligible);
    };

    // Mutable input history for one logical action.
    struct Action {
        // Declared value representation of this action.
        InputAction::ValueKind kind = InputAction::BUTTON;
        // Map membership stays fixed for the lifetime of this compiled scope.
        godot::StringName map = InputAction::DEFAULT_MAP;
        // Cached map state avoids a map lookup for every input event and physics sample.
        bool enabled = true;
        // Invalidates only this action's queued callbacks when its map changes.
        uint64_t map_generation = 0;
        // Normalized threshold for scalar, vector, and analog buttons.
        double deadzone = 0.0;
        // Whether this action is gated on desktop pointer capture.
        bool requires_pointer = false;
        // Bound physical sources in stable authoring order.
        std::vector<Binding> bindings;
        // Synthetic InputEventAction level independent of physical slot state.
        bool synthetic_held = false;
        // Physical-style neutral gating for injected Godot action events.
        bool synthetic_observed = false;
        bool synthetic_wait_for_release = false;
        // Number of presses to dispatch at the next physics boundary.
        uint32_t pending_presses = 0;
        // Whether a release awaits the next physics boundary.
        bool pending_release = false;
        // Published press and release flags for the current physics tick.
        bool frame_pressed = false;
        bool frame_released = false;
        // Relative movement accrued since the last physics boundary.
        godot::Vector2 pending_motion;
        // Relative movement published for the current physics tick.
        godot::Vector2 frame_motion;
        // Latest published normalized directional level.
        godot::Vector2 frame_level;
        // Published held value for the current physics tick.
        bool frame_held = false;
        // Callbacks for admitted press edges at the physics boundary.
        std::vector<godot::Callable> listeners;
    };

    // Identity of the callback currently owning exclusive raw input.
    godot::Callable capture_callback;
    // Weak ID of the optional scene owner of exclusive capture.
    godot::ObjectID capture_owner;
    // Handles watched for automatic suspension, indexed by Control identity.
    std::map<godot::ObjectID, bool> menus;
    // Flattened (action, binding index) for each stable binding slot.
    std::map<godot::StringName, std::pair<godot::StringName, size_t>> slot_locations;
    // Slot IDs in authoring order for settings consumers.
    std::vector<godot::StringName> slot_order;
    // Runtime actions keyed by stable logical ID.
    std::map<godot::StringName, Action> actions;
    // Action IDs in authoring order for deterministic dispatch.
    std::vector<godot::StringName> action_order;
    // Known action maps and their activation states for this owner only.
    std::map<godot::StringName, bool> maps{{godot::StringName(InputAction::DEFAULT_MAP), true}};
    // The public facade stays live while this router remains attached.
    godot::Ref<Controls> handle;
    // Identity of the source resource, separate from runtime state copies.
    godot::Ref<InputLayout> layout;
    // Which button immediately releases the captured pointer.
    godot::StringName release_action;
    // Configured gamepad ID, or -1 to reserve the first connected gamepad.
    int32_t requested_gamepad = -1;
    // Active gamepad ID when a default assignment was requested.
    int32_t active_gamepad = -1;
    // Whether this scope owns keyboard and mouse events in its viewport.
    bool use_keyboard_mouse = true;
    // Whether an unhandled left press can immediately acquire pointer capture.
    bool click_to_capture = false;
    // Whether this scope currently has permission to acquire the pointer.
    bool pointer_available = true;
    // Whether focus and all live UI scopes currently allow gameplay events.
    bool suspended = false;
    // Whether keyboard and pointer focus is currently available.
    bool focused = true;
    // Whether suspension should restore the pointer when gameplay resumes.
    bool restore_pointer = false;
    // Suppress the physical recapture button through its first release.
    bool recapture_button_down = false;
    // True once this scope has released its router and retained consumers.
    bool retired = false;
    // Incremented at input handoffs so stale callbacks are never dispatched.
    uint64_t generation = 0;

    // Snapshot the authored layout and initialize independent action state.
    void load_layout(const godot::Ref<InputLayout> &source);
    // Import ordinary Godot Project Input Map actions as buttons.
    void load_project_actions();
    // Switch the local map state and discard stale levels, edges, and callbacks in its actions.
    godot::Error set_map_enabled(const godot::StringName &map, bool enabled);
    // Register a source and its stable settings lookup in one runtime action.
    void append_binding(const godot::StringName &action_id, const godot::Ref<InputBinding> &source);
    // Read the current positive button state before physics publication.
    bool button_down(const Action &action) const;
    // Calculate one level from direction-weighted physical source strengths.
    godot::Vector2 read_level(const Action &action) const;
    // Return whether a scope and pointer gate allow this action's values.
    bool accepts_action(const Action &action) const;
    // Observe releases before GUI consumes them, or admit a new unhandled event.
    void update_actions(const godot::Ref<godot::InputEvent> &event, bool admit);
    // Accumulate an eligible button transition and apply the release shortcut.
    void record_button(const godot::StringName &id, Action &action, bool previous, bool pulse);
    // Route keyboard/mouse or a pad event to its assigned local player.
    bool accepts_device(const godot::Ref<godot::InputEvent> &event);
    // Check whether another scope in the viewport has exclusive settings capture.
    bool capture_reserved() const;
    // Check whether another live scope in the viewport has reserved this gamepad.
    bool gamepad_reserved(int32_t device) const;
    // Clear pending one-shots and relative motion, optionally preserving continuous levels.
    void invalidate(bool preserve_levels);
    // Clear one action's published and pending state, optionally preserving continuous levels.
    void invalidate_action(Action &action, bool preserve_levels);
    // Drop physical observations when this scope can no longer see the corresponding releases.
    void forget_observed_input(bool gamepad_only);
    // Publish one snapshot before gameplay physics callbacks and emit accepted presses.
    void publish();
    // Connect private scene lifetime, focus, and input callbacks.
    void start();
    // Retire the scope before invoking external lifecycle callbacks.
    void retire();
    // Release pointer and cancel capture when the game window loses focus.
    void focus_exited();
    // Restore input availability when the window is focused again.
    void focus_entered();
    // Clear pad levels and permit automatic replacement after disconnect.
    void joy_connection_changed(int64_t device, bool connected);
    // Update suspend state from watched Control visibility, focus, and rebinding.
    void refresh_suspension();
    // Refresh the effective visibility of one watched scene Control.
    void menu_changed(uint64_t instance_id);
    // Remove a watched menu when its Control leaves the tree.
    void menu_exiting(uint64_t instance_id);
    // Force-close raw capture for owner exit or focus loss.
    void force_end_capture();
    // Disconnect the raw capture owner and optionally notify its callback with nil.
    void finish_capture(bool notify);
    // Return one detached default or effective binding snapshot.
    godot::Ref<InputBinding> copy_slot(const godot::StringName &id, bool defaults) const;
    // Check whether this live router owns the process pointer lease.
    bool owns_pointer() const;
    // Release this exact lease without disturbing another pointer owner.
    void release_mouse();
    // Acquire the desktop pointer immediately, respecting the existing owner.
    godot::Error capture_mouse();

protected:
    // No public bindings on this internal engine node.
    static void _bind_methods();
    // Retire this input scope whenever the owner leaves the scene.
    void _notification(int what);

public:
    // Track raw releases and optionally intercept device capture before GUI dispatch.
    void _input(const godot::Ref<godot::InputEvent> &event) override;
    // Admit new gameplay actions only after Godot UI has declined the event.
    void _unhandled_input(const godot::Ref<godot::InputEvent> &event) override;
};

} // namespace crimson::input
