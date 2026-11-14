// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#pragma once

#include "input_modifiers.hpp"

#include <godot_cpp/classes/control.hpp>
#include <godot_cpp/classes/input_event.hpp>
#include <godot_cpp/classes/resource.hpp>
#include <godot_cpp/core/object.hpp>
#include <godot_cpp/variant/typed_array.hpp>

namespace crimson::input {

// One stable binding slot in an authored action or a detached runtime snapshot.
class InputBinding : public godot::Resource {
    GDCLASS(InputBinding, godot::Resource)

public:
    // A paired analog source for a two-dimensional action.
    enum Stick { NONE, LEFT_STICK, RIGHT_STICK };
    // Screen-axis channels carried by a mouse motion event.
    enum MouseAxes { MOUSE_XY, MOUSE_X, MOUSE_Y };

private:
    // Stable binding identity, independent of the assigned physical control.
    godot::StringName id;
    // Logical owner on a runtime snapshot. Authoring derives it from containment.
    godot::StringName action;
    // Optional human-readable label for a remapping screen.
    godot::String display_name;
    // Physical source, or an empty reference for an unassigned slot.
    godot::Ref<godot::InputEvent> event;
    // Contribution of the source to the action's value.
    godot::Vector3 contribution{1, 0, 0};
    // Optional whole-stick source in place of a scalar physical event.
    Stick stick = NONE;
    // Raw screen axes retained for mouse motion. Other sources ignore this field.
    MouseAxes mouse_axes = MOUSE_XY;
    // Ordered transforms applied to this source before action aggregation.
    godot::TypedArray<InputModifier> modifiers;

protected:
    // Bind stable slot metadata and Inspector fields.
    static void _bind_methods();

public:
    // Assign the unique binding ID within a layout.
    void set_id(const godot::StringName &value);
    // Return the stable binding ID.
    godot::StringName get_id() const;
    // Set the source action on a detached snapshot.
    void set_action(const godot::StringName &value);
    // Return the source action on a detached snapshot.
    godot::StringName get_action() const;
    // Assign the optional settings label.
    void set_display_name(const godot::String &value);
    // Return the optional settings label.
    godot::String get_display_name() const;
    // Assign a physical control, or clear the binding with an empty reference.
    void set_event(const godot::Ref<godot::InputEvent> &value);
    // Return the physical control assigned to this slot.
    godot::Ref<godot::InputEvent> get_event() const;
    // Set the direction of an axis source or pixel scaling for mouse motion.
    void set_contribution(const godot::Vector3 &value);
    // Return the direction or motion scaling of this binding.
    godot::Vector3 get_contribution() const;
    // Assign a paired analog source and clear any event when selecting a whole stick.
    void set_stick(Stick value);
    // Return the paired analog source, or NONE for a physical event slot.
    Stick get_stick() const;
    // Choose the raw screen axes for this mouse motion event.
    void set_mouse_axes(MouseAxes value);
    // Return the raw screen axes selected for mouse motion.
    MouseAxes get_mouse_axes() const;
    // Replace the source with relative mouse motion using unscaled pixels.
    void set_mouse_motion(MouseAxes axes = MOUSE_XY);
    // Replace the ordered modifier stack.
    void set_modifiers(const godot::TypedArray<InputModifier> &value);
    // Return the ordered modifier stack.
    godot::TypedArray<InputModifier> get_modifiers() const;
};

// A typed logical action and the authored sources that contribute to it.
class InputAction : public godot::Resource {
    GDCLASS(InputAction, godot::Resource)

public:
    // The map used by new and previously serialized actions without an authored map.
    static constexpr const char *DEFAULT_MAP = "Gameplay";
    // The stable value representation produced by this action.
    enum ValueKind { BUTTON, AXIS, VECTOR, MOTION };
    // Create an independent action definition for use by map entries.
    static godot::Ref<InputAction> create(ValueKind kind);

private:
    // Stable logical identity shared by scripts, layouts, and bindings.
    godot::StringName id;
    // Optional human-readable action label.
    godot::String display_name;
    // Name of the layout group that enables this action together with its peers.
    godot::StringName map = DEFAULT_MAP;
    // Value returned by the action's typed reader.
    ValueKind kind = BUTTON;
    // Neutral radius for scalar and vector levels, in normalized units.
    double deadzone = 0.0;
    // Whether the action requires this player's desktop pointer capture.
    bool requires_pointer = false;
    // Physical sources in authoring order.
    godot::TypedArray<InputBinding> bindings;

protected:
    // Bind authored action identity, shape, and physical sources.
    static void _bind_methods();

public:
    // Assign the unique action ID within a layout.
    void set_id(const godot::StringName &value);
    // Return the stable logical action ID.
    godot::StringName get_id() const;
    // Assign the optional human-readable action label.
    void set_display_name(const godot::String &value);
    // Return the optional human-readable action label.
    godot::String get_display_name() const;
    // Assign this action to a named map in its layout.
    void set_map(const godot::StringName &value);
    // Return the action's authored map name.
    godot::StringName get_map() const;
    // Set the typed value produced by this action.
    void set_kind(ValueKind value);
    // Return the value type consumed by Controls.
    ValueKind get_kind() const;
    // Set a normalized neutral radius in the range [0, 1).
    void set_deadzone(double value);
    // Return the normalized neutral radius.
    double get_deadzone() const;
    // Gate this action on active desktop pointer capture.
    void set_requires_pointer(bool value);
    // Report whether this action requires desktop pointer capture.
    bool get_requires_pointer() const;
    // Replace the authored binding list.
    void set_bindings(const godot::TypedArray<InputBinding> &value);
    // Return the authored binding list.
    godot::TypedArray<InputBinding> get_bindings() const;
};

// One map-local declaration connecting a logical ID, action behavior, and physical slots.
class InputActionMapping : public godot::Resource {
    GDCLASS(InputActionMapping, godot::Resource)

    // Stable logical identity used by Controls and gameplay scripts.
    godot::StringName id;
    // Reusable definition of the value type and runtime gates.
    godot::Ref<InputAction> action;
    // Ordered physical sources for this declaration.
    godot::TypedArray<InputBinding> bindings;

protected:
    // Bind map entry authoring properties.
    static void _bind_methods();

public:
    // Assign the logical ID read by Controls.
    void set_id(const godot::StringName &value);
    // Return the logical ID read by Controls.
    godot::StringName get_id() const;
    // Assign the reusable action definition.
    void set_action(const godot::Ref<InputAction> &value);
    // Return the action definition referenced by this mapping.
    godot::Ref<InputAction> get_action() const;
    // Replace the ordered physical sources for this declaration.
    void set_bindings(const godot::TypedArray<InputBinding> &value);
    // Return the ordered physical sources for this declaration.
    godot::TypedArray<InputBinding> get_bindings() const;
    // Append one physical source and return this mapping for further configuration.
    godot::Ref<InputActionMapping> add_binding(const godot::Ref<InputBinding> &value);
    // Validate this declaration, including its action reference and source compatibility.
    godot::Error validate() const;
};

// Independently editable set of mappings enabled together for a Controls scope.
class InputActionMap : public godot::Resource {
    GDCLASS(InputActionMap, godot::Resource)

    // Identity read by Controls. Gameplay is active at boot.
    godot::StringName id = InputAction::DEFAULT_MAP;
    // Optional readable title for tools.
    godot::String display_name;
    // Declared actions and physical slots in authoring order.
    godot::TypedArray<InputActionMapping> mappings;

protected:
    // Bind independent map asset authoring properties.
    static void _bind_methods();

public:
    // Create a map draft for a logical map ID.
    static godot::Ref<InputActionMap> create(const godot::StringName &id);
    // Set the ID used to enable or disable the map.
    void set_id(const godot::StringName &value);
    // Return the ID used to enable or disable the map.
    godot::StringName get_id() const;
    // Set the optional title displayed by authoring tools.
    void set_display_name(const godot::String &value);
    // Return the optional title displayed by authoring tools.
    godot::String get_display_name() const;
    // Replace the ordered mappings in the map.
    void set_mappings(const godot::TypedArray<InputActionMapping> &value);
    // Return the ordered mappings in the map.
    godot::TypedArray<InputActionMapping> get_mappings() const;
    // Append one mapping and return it so the caller can attach physical bindings.
    godot::Ref<InputActionMapping> add_action(const godot::StringName &id, const godot::Ref<InputAction> &action);
    // Validate this map and its unique action and binding IDs.
    godot::Error validate() const;
};

// Editable input defaults shared by independent local Controls scopes.
class InputLayout : public godot::Resource {
    GDCLASS(InputLayout, godot::Resource)

    // Legacy inline actions in authoring order.
    godot::TypedArray<InputAction> actions;
    // Standalone map assets in authoring order.
    godot::TypedArray<InputActionMap> maps;
    // Whether an eligible left click captures a released pointer.
    bool click_to_capture = false;
    // Optional button action that releases the captured pointer immediately.
    godot::StringName release_action;

    // Append a composite vector from four directions on one analog stick.
    godot::Ref<InputLayout> add_stick(const godot::StringName &id, double deadzone, bool right, bool keyboard);

protected:
    // Bind Inspector defaults and equivalent code-first authoring.
    static void _bind_methods();

public:
    // Create an empty code-first layout draft.
    static godot::Ref<InputLayout> create();
    // Replace the authored actions in this draft.
    void set_actions(const godot::TypedArray<InputAction> &value);
    // Return inline compatibility actions in their configured order.
    godot::TypedArray<InputAction> get_actions() const;
    // Replace the standalone map assets in this layout.
    void set_maps(const godot::TypedArray<InputActionMap> &value);
    // Return standalone map assets in their configured order.
    godot::TypedArray<InputActionMap> get_maps() const;
    // Append a standalone map and return the layout for further configuration.
    godot::Ref<InputLayout> with_map(const godot::Ref<InputActionMap> &value);
    // Return inline actions followed by resolved map actions for runtime lookup.
    godot::TypedArray<InputAction> resolved_actions() const;
    // Opt into recapturing on a fresh left click that reaches gameplay.
    void set_click_to_capture(bool value);
    // Report whether unhandled clicks can recapture.
    bool get_click_to_capture() const;
    // Name the button that releases the pointer, or clear automatic release.
    void set_release_action(const godot::StringName &value);
    // Return the configured pointer-release action.
    godot::StringName get_release_action() const;
    // Verify that this draft can boot without mutating any input scope.
    godot::Error validate() const;
    // Verify the type of an authored action.
    bool has_action_kind(const godot::StringName &id, InputAction::ValueKind kind) const;
    // Append a normalized WASD and left-stick movement vector.
    godot::Ref<InputLayout> add_move(const godot::StringName &id, double deadzone = 0.18);
    // Append captured relative pointer motion in unscaled screen pixels.
    godot::Ref<InputLayout> add_mouse_motion(const godot::StringName &id);
    // Append a normalized right-stick vector.
    godot::Ref<InputLayout> add_right_stick(const godot::StringName &id, double deadzone = 0.18);
    // Append a button with a physical key and optional gamepad binding.
    godot::Ref<InputLayout> add_button(const godot::StringName &id, godot::Key key, godot::JoyButton button = godot::JOY_BUTTON_INVALID);
};

class InputRouter;

// Code-first scoped action reads, pointer control, and modal input suspension.
class Controls : public godot::RefCounted {
    GDCLASS(Controls, godot::RefCounted)

    friend class InputRouter;

    // Weak identity of the router attached to the owning scene node.
    godot::ObjectID router_id;
    // Result of creating or acquiring the requested scope.
    godot::Error error = godot::ERR_UNCONFIGURED;

    // Resolve this scope's live router from its exact attachment.
    InputRouter *resolve() const;

protected:
    // Bind the common action facade and its lifecycle signals.
    static void _bind_methods();

public:
    // Attach local input under an existing node or resolve an equivalent attachment.
    static godot::Ref<Controls> boot(godot::Node *owner, const godot::Ref<InputLayout> &layout = {}, int32_t joypad = -1, bool keyboard_and_mouse = true);
    // Find the nearest ready Controls attached to this node or an ancestor.
    static godot::Ref<Controls> for_node(godot::Node *node);
    // Report whether this handle has a live scene owner.
    bool is_ready() const;
    // Return the original attachment status.
    godot::Error get_error() const;
    // Return the owning scene node while this scope is live.
    godot::Node *get_owner() const;
    // Return the authored layout identity, or null for imported project actions.
    godot::Ref<InputLayout> get_layout() const;
    // Verify the type of a registered logical action in this runtime scope.
    bool has_action_kind(const godot::StringName &id, InputAction::ValueKind kind) const;
    // Change which actions are routed for this local player, without editing the layout.
    godot::Error set_map_enabled(const godot::StringName &map, bool enabled);
    // Report whether this local player currently has the named action map enabled.
    bool is_map_enabled(const godot::StringName &map) const;
    // Return a press admitted for the current physics tick.
    bool pressed(const godot::StringName &id) const;
    // Return the current logical held value of a button.
    bool held(const godot::StringName &id) const;
    // Return a release admitted for the current physics tick.
    bool released(const godot::StringName &id) const;
    // Return a normalized one-dimensional level in the range [-1, 1].
    double axis(const godot::StringName &id) const;
    // Return a normalized vector level with magnitude at most one.
    godot::Vector2 vector(const godot::StringName &id) const;
    // Return this physics tick's relative pointer motion in unscaled pixels.
    godot::Vector2 motion(const godot::StringName &id) const;
    // Call a no-argument handler on every admitted press of this button.
    godot::Error on_press(const godot::StringName &id, const godot::Callable &callback);
    // Disconnect the exact previously registered press callback.
    godot::Error off_press(const godot::StringName &id, const godot::Callable &callback);
    // Acquire desktop pointer capture immediately for this focused scope.
    godot::Error capture_pointer();
    // Release desktop pointer capture immediately if owned by this scope.
    godot::Error release_pointer();
    // Report effective pointer ownership, including native capture state.
    bool pointer_captured() const;
    // Disable or reenable desktop pointer acquisition for this local scope.
    void set_pointer_available(bool available);
    // Suspend this scope automatically while the supplied menu is visible.
    godot::Error suspend_while_visible(godot::Control *menu);
    // Unwatch the supplied menu and recompute suspension immediately.
    godot::Error stop_suspending_while_visible(godot::Control *menu);
    // Report neutralization by a visible menu, capture session, or window focus loss.
    bool is_suspended() const;
    // Retire the attachment and release the devices and pointer it owns.
    void close();

    // Return detached copies of the current binding slots in authoring order.
    godot::TypedArray<InputBinding> bindings() const;
    // Return a detached copy of one current binding slot, or null when unknown.
    godot::Ref<InputBinding> binding(const godot::StringName &id) const;
    // Return a detached copy of the authored binding slot, or null when unknown.
    godot::Ref<InputBinding> default_binding(const godot::StringName &id) const;
    // Atomically replace one physical source in the local overlay.
    godot::Error replace_binding(const godot::StringName &id, const godot::Ref<godot::InputEvent> &event);
    // Atomically replace physical sources for a list of existing slots.
    godot::Error replace_bindings(const godot::TypedArray<InputBinding> &edits);
    // Restore one local slot to its authored default.
    godot::Error reset_binding(const godot::StringName &id);
    // Restore all local slots to their authored defaults.
    void reset_bindings();
    // Capture raw assigned device events exclusively for a settings action.
    godot::Error begin_capture(const godot::Callable &callback, godot::Node *owner = nullptr);
    // Report whether a settings action already owns exclusive capture in this scope.
    bool is_capturing() const;
    // End capture only when the exact installed callback requests it.
    void end_capture(const godot::Callable &callback);
};

} // namespace crimson::input

VARIANT_ENUM_CAST(crimson::input::InputAction::ValueKind);
VARIANT_ENUM_CAST(crimson::input::InputBinding::Stick);
VARIANT_ENUM_CAST(crimson::input::InputBinding::MouseAxes);
