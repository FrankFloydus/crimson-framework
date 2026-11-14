// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#pragma once

#include "modules/input/controls.hpp"

#include <godot_cpp/classes/scene_tree_timer.hpp>

#include <set>
#include <utility>

namespace crimson::rebinding {

// Detached information about one editable binding in a local Controls scope.
class BindingSlot : public godot::RefCounted {
    GDCLASS(BindingSlot, godot::RefCounted)

    friend class Rebinding;

    // Exact Controls attachment from which this snapshot was read.
    godot::ObjectID controls_id;
    // Stable binding identity within the layout.
    godot::StringName id;
    // Action to which this slot contributes.
    godot::StringName action;
    // Physical control family represented by the Rebinding device enum.
    int32_t device = 0;
    // Printable name of the currently assigned physical control.
    godot::String label;
    // Optional game-authored label of the slot's action or direction.
    godot::String display_name;
    // Snapshot of the assigned event. Mutable copies never modify Controls.
    godot::Ref<godot::InputEvent> event;

protected:
    // Bind the read-only snapshot properties.
    static void _bind_methods();

public:
    // Return the stable binding identity within the layout.
    godot::StringName get_id() const;
    // Return the logical action name.
    godot::StringName get_action() const;
    // Return the Rebinding device enum value.
    int32_t get_device() const;
    // Return the current physical control's readable label.
    godot::String get_label() const;
    // Return the optional game-authored label.
    godot::String get_display_name() const;
    // Return a detached copy of the physical control, or null if unbound.
    godot::Ref<godot::InputEvent> get_event() const;
};

// Terminal status of one capture or default-restoration request.
class RebindResult : public godot::RefCounted {
    GDCLASS(RebindResult, godot::RefCounted)

    friend class Rebinding;

    // Stable result code, with Ok reserved for successful completion.
    godot::StringName code = "Unavailable";
    // Short status suitable for a settings panel.
    godot::String message;
    // Fresh binding snapshot after a successful single-slot edit.
    godot::Ref<BindingSlot> slot;

protected:
    // Bind the read-only outcome properties.
    static void _bind_methods();

public:
    // Report whether the requested change was accepted.
    bool is_ok() const;
    // Return the stable result code.
    godot::StringName get_code() const;
    // Return the displayable outcome.
    godot::String get_message() const;
    // Return the fresh snapshot for a successful single-slot edit, otherwise null.
    godot::Ref<BindingSlot> get_slot() const;
};

// Retains the distinct completion signal for one pending capture.
class RebindOperation : public godot::RefCounted {
    GDCLASS(RebindOperation, godot::RefCounted)

    friend class Rebinding;

    // Stored before signaling so completion during capture startup can be returned directly.
    godot::Ref<RebindResult> outcome;

protected:
    // Bind the operation's typed completion signal.
    static void _bind_methods();
};

// Code-first editing of a single local Controls binding overlay.
class Rebinding : public godot::RefCounted {
    GDCLASS(Rebinding, godot::RefCounted)

public:
    // Select the family of physical controls for a binding or capture.
    enum BindingDevice { KEYBOARD_MOUSE, GAMEPAD };

private:
    // Existing local Controls scope. Its router is owned by the game scene.
    godot::Ref<input::Controls> controls;
    // Exact modal owner, or zero when the Controls owner is used.
    godot::ObjectID owner_id;
    // Operation retained until its exclusive capture settles.
    godot::Ref<RebindOperation> pending;
    // Slot snapshot selected at the start of capture.
    godot::Ref<BindingSlot> selected;
    // Exact bound callback installed in Controls for the current operation.
    godot::Callable capture;
    // Deadline running independently of pause and game time scale.
    godot::Ref<godot::SceneTreeTimer> deadline;
    // Last newly pressed modifier, committed on its release if no chord arrived.
    godot::Ref<godot::InputEvent> modifier;
    // Axes held when capture starts. A neutral event arms each axis again.
    std::set<std::pair<int32_t, int32_t>> held_axes;

    // Return the explicit modal owner or the Controls owner while still active.
    godot::Node *get_owner() const;
    // Create a detached slot for an editable binding and its authored device family.
    godot::Ref<BindingSlot> describe(const godot::Ref<input::InputBinding> &binding) const;
    // Verify that a snapshot addresses a slot from this Controls attachment.
    bool owns(const godot::Ref<BindingSlot> &slot) const;
    // Construct one stable completion value.
    static godot::Ref<RebindResult> result(const godot::StringName &code, const godot::String &message,
            const godot::Ref<BindingSlot> &slot = {});
    // Observe a raw input event, or nil when the router cancels capture.
    void on_capture(const godot::Variant &value, const godot::Ref<Rebinding> &retained,
            const godot::Ref<RebindOperation> &operation);
    // End a capture that did not receive a new physical control by its deadline.
    void on_timeout();
    // Clear capture resources before notifying script observers of settlement.
    void finish(const godot::Ref<RebindResult> &outcome);

protected:
    // Bind the facade, device enum, and committed-binding change signal.
    static void _bind_methods();

public:
    // Open a binding editor for a local scope and optional modal lifetime.
    static godot::Ref<Rebinding> open(const godot::Ref<input::Controls> &controls, godot::Node *owner = nullptr);
    // Return editable slots in authored order.
    godot::TypedArray<BindingSlot> slots() const;
    // Return the first matching slot in authored order, or null.
    godot::Ref<BindingSlot> slot(const godot::StringName &action, BindingDevice device) const;
    // Return one exact editable slot, or null.
    godot::Ref<BindingSlot> slot_by_id(const godot::StringName &id) const;
    // Start a bounded exclusive capture and return its completion signal or immediate result.
    godot::Variant rebind(const godot::Ref<BindingSlot> &slot);
    // Cancel this editor's pending capture. Return ERR_DOES_NOT_EXIST if already idle.
    godot::Error cancel();
    // Restore one slot to its authored default and return the outcome.
    godot::Ref<RebindResult> reset(const godot::Ref<BindingSlot> &slot);
    // Restore every binding to its authored default and return the outcome.
    godot::Ref<RebindResult> reset_all();
    // Write a bounded, versioned local profile of the edited slots to user://.
    godot::Error save(const godot::String &path) const;
    // Validate and apply the saved profile as one overlay replacement.
    godot::Error load(const godot::String &path);
};

} // namespace crimson::rebinding

VARIANT_ENUM_CAST(crimson::rebinding::Rebinding::BindingDevice);
