// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#include "rebinding.hpp"

#include <godot_cpp/classes/display_server.hpp>
#include <godot_cpp/classes/input.hpp>
#include <godot_cpp/classes/input_event_joypad_button.hpp>
#include <godot_cpp/classes/input_event_joypad_motion.hpp>
#include <godot_cpp/classes/input_event_key.hpp>
#include <godot_cpp/classes/input_event_mouse_button.hpp>
#include <godot_cpp/classes/os.hpp>
#include <godot_cpp/classes/scene_tree.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/callable_method_pointer.hpp>
#include <godot_cpp/variant/signal.hpp>

#include <cmath>
#include <optional>

namespace crimson::rebinding {
using namespace godot;
using input::Controls;
using input::InputBinding;

namespace {

// Capture expires in real seconds even while the tree is paused.
constexpr double capture_seconds = 15.0;
// An axis must reach this magnitude to become a deliberate binding.
constexpr float axis_press = 0.65f;
// Previously held axes must pass through this neutral range first.
constexpr float axis_neutral = 0.25f;

// Classify only editable physical events. Relative mouse motion is not a binding candidate.
std::optional<Rebinding::BindingDevice> editable_device(const Ref<InputEvent> &event) {
    if (Ref<InputEventKey>(event).is_valid() || Ref<InputEventMouseButton>(event).is_valid()) {
        return Rebinding::KEYBOARD_MOUSE;
    }
    if (Ref<InputEventJoypadButton>(event).is_valid() || Ref<InputEventJoypadMotion>(event).is_valid()) {
        return Rebinding::GAMEPAD;
    }
    return std::nullopt;
}

// Keep scene and input operations on the main thread.
bool on_main_thread() {
    const OS *os = OS::get_singleton();
    return os && os->get_main_thread_id() == os->get_thread_caller_id();
}

// Identify an individual keyboard control without losing physical layout information.
Key event_key(const Ref<InputEventKey> &key) {
    return key->get_physical_keycode() != KEY_NONE ? key->get_physical_keycode() : key->get_keycode();
}

// Use the current keyboard layout for physical keys while preserving chord modifiers.
String event_label(const Ref<InputEvent> &event) {
    if (event.is_null()) {
        return "Unbound";
    }
    const Ref<InputEventKey> key = event;
    if (key.is_null()) {
        return event->as_text();
    }
    Key label = event_key(key);
    const auto *display = DisplayServer::get_singleton();
    if (key->get_physical_keycode() != KEY_NONE && display && display->get_name() != "headless") {
        const Key localized = display->keyboard_get_label_from_physical(key->get_physical_keycode());
        if (localized != KEY_NONE && localized != KEY_UNKNOWN) {
            label = localized;
        }
    }
    const auto *os = OS::get_singleton();
    return os ? os->get_keycode_string(static_cast<Key>(static_cast<uint64_t>(label) | key->get_modifiers_mask())) :
                event->as_text();
}

// A modifier is accepted on release unless a subsequent key captured a chord first.
bool is_modifier(Key key) {
    return key == KEY_SHIFT || key == KEY_CTRL || key == KEY_ALT || key == KEY_META;
}

// Copy just the chord flags that participate in local input matching.
void copy_modifiers(const Ref<InputEventWithModifiers> &target, const Ref<InputEventWithModifiers> &source) {
    target->set_alt_pressed(source->is_alt_pressed());
    target->set_shift_pressed(source->is_shift_pressed());
    target->set_ctrl_pressed(source->is_ctrl_pressed());
    target->set_meta_pressed(source->is_meta_pressed());
}

// Normalize a raw event to its physical binding. Do not store transient event state.
Ref<InputEvent> normalize(const Ref<InputEvent> &source, const Ref<InputEvent> &previous) {
    if (const Ref<InputEventKey> key = source; key.is_valid()) {
        Ref<InputEventKey> event;
        event.instantiate();
        const Ref<InputEventKey> old_key = previous;
        if (old_key.is_valid() && old_key->get_physical_keycode() == KEY_NONE && key->get_keycode() != KEY_NONE) {
            event->set_keycode(key->get_keycode());
        } else if (key->get_physical_keycode() != KEY_NONE) {
            event->set_physical_keycode(key->get_physical_keycode());
        } else {
            event->set_keycode(key->get_keycode());
        }
        if (!is_modifier(event_key(key))) {
            copy_modifiers(event, key);
        }
        return event;
    }
    if (const Ref<InputEventMouseButton> mouse = source; mouse.is_valid()) {
        Ref<InputEventMouseButton> event;
        event.instantiate();
        event->set_button_index(mouse->get_button_index());
        copy_modifiers(event, mouse);
        return event;
    }
    const int32_t device = previous.is_valid() ? previous->get_device() : -1;
    if (const Ref<InputEventJoypadButton> pad = source; pad.is_valid()) {
        Ref<InputEventJoypadButton> event;
        event.instantiate();
        event->set_button_index(pad->get_button_index());
        event->set_device(device);
        return event;
    }
    const Ref<InputEventJoypadMotion> axis = source;
    if (axis.is_valid()) {
        Ref<InputEventJoypadMotion> event;
        event.instantiate();
        event->set_axis(axis->get_axis());
        event->set_axis_value(axis->get_axis_value() < 0.0f ? -1.0f : 1.0f);
        event->set_device(device);
        return event;
    }
    return {};
}

} // namespace

void BindingSlot::_bind_methods() {
    ClassDB::bind_method(D_METHOD("get_id"), &BindingSlot::get_id);
    ClassDB::bind_method(D_METHOD("get_action"), &BindingSlot::get_action);
    ClassDB::bind_method(D_METHOD("get_device"), &BindingSlot::get_device);
    ClassDB::bind_method(D_METHOD("get_label"), &BindingSlot::get_label);
    ClassDB::bind_method(D_METHOD("get_display_name"), &BindingSlot::get_display_name);
    ClassDB::bind_method(D_METHOD("get_event"), &BindingSlot::get_event);
    ADD_PROPERTY(PropertyInfo(Variant::STRING_NAME, "id", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_NONE), "", "get_id");
    ADD_PROPERTY(PropertyInfo(Variant::STRING_NAME, "action", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_NONE), "", "get_action");
    ADD_PROPERTY(PropertyInfo(Variant::INT, "device", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_NONE), "", "get_device");
    ADD_PROPERTY(PropertyInfo(Variant::STRING, "label", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_NONE), "", "get_label");
    ADD_PROPERTY(PropertyInfo(Variant::STRING, "display_name", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_NONE), "", "get_display_name");
    ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "event", PROPERTY_HINT_RESOURCE_TYPE, "InputEvent", PROPERTY_USAGE_NONE), "", "get_event");
}

StringName BindingSlot::get_id() const { return id; }
StringName BindingSlot::get_action() const { return action; }
int32_t BindingSlot::get_device() const { return device; }
String BindingSlot::get_label() const { return label; }
String BindingSlot::get_display_name() const { return display_name; }

Ref<InputEvent> BindingSlot::get_event() const {
    return event.is_valid() ? Ref<InputEvent>(event->duplicate()) : Ref<InputEvent>();
}

void RebindResult::_bind_methods() {
    ClassDB::bind_method(D_METHOD("is_ok"), &RebindResult::is_ok);
    ClassDB::bind_method(D_METHOD("get_code"), &RebindResult::get_code);
    ClassDB::bind_method(D_METHOD("get_message"), &RebindResult::get_message);
    ClassDB::bind_method(D_METHOD("get_slot"), &RebindResult::get_slot);
    ADD_PROPERTY(PropertyInfo(Variant::BOOL, "ok", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_NONE), "", "is_ok");
    ADD_PROPERTY(PropertyInfo(Variant::STRING_NAME, "code", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_NONE), "", "get_code");
    ADD_PROPERTY(PropertyInfo(Variant::STRING, "message", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_NONE), "", "get_message");
    ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "slot", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_NONE, "BindingSlot"), "", "get_slot");
}

bool RebindResult::is_ok() const { return code == StringName("Ok"); }
StringName RebindResult::get_code() const { return code; }
String RebindResult::get_message() const { return message; }
Ref<BindingSlot> RebindResult::get_slot() const { return slot; }

void RebindOperation::_bind_methods() {
    ADD_SIGNAL(MethodInfo("completed", PropertyInfo(Variant::OBJECT, "result", PROPERTY_HINT_NONE,
            "", PROPERTY_USAGE_DEFAULT, "RebindResult")));
}

void Rebinding::_bind_methods() {
    ClassDB::bind_static_method(get_class_static(), D_METHOD("open", "controls", "owner"), &Rebinding::open, DEFVAL(Variant()));
    ClassDB::bind_method(D_METHOD("slots"), &Rebinding::slots);
    ClassDB::bind_method(D_METHOD("slot", "action", "device"), &Rebinding::slot);
    ClassDB::bind_method(D_METHOD("slot_by_id", "id"), &Rebinding::slot_by_id);
    ClassDB::bind_method(D_METHOD("rebind", "slot"), &Rebinding::rebind);
    ClassDB::bind_method(D_METHOD("cancel"), &Rebinding::cancel);
    ClassDB::bind_method(D_METHOD("reset", "slot"), &Rebinding::reset);
    ClassDB::bind_method(D_METHOD("reset_all"), &Rebinding::reset_all);
    ClassDB::bind_method(D_METHOD("save", "path"), &Rebinding::save);
    ClassDB::bind_method(D_METHOD("load", "path"), &Rebinding::load);
    ADD_SIGNAL(MethodInfo("changed", PropertyInfo(Variant::OBJECT, "slot", PROPERTY_HINT_NONE,
            "", PROPERTY_USAGE_DEFAULT, "BindingSlot")));
    BIND_ENUM_CONSTANT(KEYBOARD_MOUSE);
    BIND_ENUM_CONSTANT(GAMEPAD);
}

Ref<Rebinding> Rebinding::open(const Ref<Controls> &scope, Node *owner) {
    Ref<Rebinding> editor;
    editor.instantiate();
    editor->controls = scope;
    editor->owner_id = owner ? ObjectID(owner->get_instance_id()) : ObjectID();
    return editor;
}

Node *Rebinding::get_owner() const {
    if (!on_main_thread() || controls.is_null() || !controls->is_ready()) {
        return nullptr;
    }
    Node *scope_owner = controls->get_owner();
    Node *owner = owner_id.is_valid() ? Object::cast_to<Node>(ObjectDB::get_instance(owner_id)) : scope_owner;
    return scope_owner && owner && owner->is_inside_tree() && !owner->is_queued_for_deletion() &&
                    owner->get_viewport() == scope_owner->get_viewport() ? owner : nullptr;
}

Ref<BindingSlot> Rebinding::describe(const Ref<InputBinding> &binding) const {
    if (binding.is_null()) {
        return {};
    }
    const auto event = binding->get_event();
    auto device = editable_device(event);
    if (event.is_null()) {
        const auto fallback = controls->default_binding(binding->get_id());
        device = fallback.is_valid() ? editable_device(fallback->get_event()) : std::nullopt;
    }
    if (!device) {
        return {};
    }
    Ref<BindingSlot> snapshot;
    snapshot.instantiate();
    snapshot->controls_id = ObjectID(controls->get_instance_id());
    snapshot->id = binding->get_id();
    snapshot->action = binding->get_action();
    snapshot->device = *device;
    snapshot->label = event_label(event);
    snapshot->display_name = binding->get_display_name();
    snapshot->event = event;
    return snapshot;
}

bool Rebinding::owns(const Ref<BindingSlot> &target) const {
    if (target.is_null() || controls.is_null() || target->controls_id != ObjectID(controls->get_instance_id())) {
        return false;
    }
    const auto current = slot_by_id(target->id);
    return current.is_valid() && current->action == target->action && current->device == target->device;
}

TypedArray<BindingSlot> Rebinding::slots() const {
    TypedArray<BindingSlot> result_slots;
    if (get_owner()) {
        const auto bindings = controls->bindings();
        for (int64_t i = 0; i < bindings.size(); ++i) {
            if (const auto snapshot = describe(bindings[i]); snapshot.is_valid()) {
                result_slots.push_back(snapshot);
            }
        }
    }
    return result_slots;
}

Ref<BindingSlot> Rebinding::slot(const StringName &action, BindingDevice device) const {
    const auto available = slots();
    for (int64_t i = 0; i < available.size(); ++i) {
        const Ref<BindingSlot> candidate = available[i];
        if (candidate->action == action && candidate->device == device) {
            return candidate;
        }
    }
    return {};
}

Ref<BindingSlot> Rebinding::slot_by_id(const StringName &id) const {
    return get_owner() ? describe(controls->binding(id)) : Ref<BindingSlot>();
}

Ref<RebindResult> Rebinding::result(const StringName &code, const String &message, const Ref<BindingSlot> &slot) {
    Ref<RebindResult> outcome;
    outcome.instantiate();
    outcome->code = code;
    outcome->message = message;
    outcome->slot = slot;
    return outcome;
}

Variant Rebinding::rebind(const Ref<BindingSlot> &target) {
    Node *owner = get_owner();
    if (!owner) {
        return result("Unavailable", "Open settings with a live Controls scope.");
    }
    if (pending.is_valid()) {
        return result("Busy", "Finish the current binding change first.");
    }
    if (!owns(target)) {
        return result("InvalidSlot", "Select a binding from these controls.");
    }

    selected = slot_by_id(target->id);
    pending.instantiate();
    const Ref<RebindOperation> operation = pending;
    capture = callable_mp(this, &Rebinding::on_capture).bind(Ref<Rebinding>(this), operation);
    const Callable installed_capture = capture;
    const Error started = controls->begin_capture(installed_capture, owner);
    if (started != OK) {
        capture = Callable();
        pending.unref();
        selected.unref();
        return started == ERR_BUSY ? result("Busy", "Finish the other binding change first.") :
                                     result("Unavailable", "Input capture is unavailable.");
    }
    if (operation->outcome.is_valid()) {
        return operation->outcome;
    }
    owner = get_owner();
    if (!owner) {
        const auto outcome = result("Cancelled", "Binding change cancelled.");
        finish(outcome);
        return outcome;
    }
    if (selected->device == GAMEPAD) {
        Input *input = Input::get_singleton();
        const TypedArray<int32_t> pads = input->get_connected_joypads();
        for (int64_t i = 0; i < pads.size(); ++i) {
            const int32_t device = pads[i];
            for (int32_t axis = 0; axis < JOY_AXIS_MAX; ++axis) {
                if (std::abs(input->get_joy_axis(device, static_cast<JoyAxis>(axis))) > axis_neutral) {
                    held_axes.emplace(device, axis);
                }
            }
        }
    }
    deadline = owner->get_tree()->create_timer(capture_seconds, true, false, true);
    deadline->connect("timeout", callable_mp(this, &Rebinding::on_timeout), Object::CONNECT_ONE_SHOT);
    return Signal(operation.ptr(), "completed");
}

void Rebinding::on_capture(const Variant &value, const Ref<Rebinding> &, const Ref<RebindOperation> &operation) {
    if (pending.is_null() || pending != operation) {
        return;
    }
    if (value.get_type() == Variant::NIL || !get_owner()) {
        finish(result("Cancelled", "Binding change cancelled."));
        return;
    }
    const Ref<InputEvent> raw = value;
    if (raw.is_null() || raw->is_echo() || raw->is_canceled()) {
        return;
    }
    const Ref<InputEventKey> key = raw;
    const Ref<InputEventJoypadButton> button = raw;
    if ((key.is_valid() && key->is_pressed() && event_key(key) == KEY_ESCAPE) ||
            (button.is_valid() && button->is_pressed() && button->get_button_index() == JOY_BUTTON_BACK)) {
        finish(result("Cancelled", "Binding change cancelled."));
        return;
    }
    if (editable_device(raw) != selected->device) {
        return;
    }
    const Ref<InputEventJoypadMotion> axis = raw;
    if (axis.is_valid()) {
        const auto identity = std::make_pair(axis->get_device(), static_cast<int32_t>(axis->get_axis()));
        const float magnitude = std::abs(axis->get_axis_value());
        if (magnitude <= axis_neutral) {
            held_axes.erase(identity);
        }
        if (!std::isfinite(magnitude) || held_axes.count(identity) || magnitude < axis_press) {
            return;
        }
    } else if (key.is_valid() && is_modifier(event_key(key))) {
        if (key->is_pressed()) {
            modifier = raw;
            return;
        }
        const Ref<InputEventKey> pressed = modifier;
        if (pressed.is_null() || event_key(pressed) != event_key(key)) {
            return;
        }
    } else if (!raw->is_pressed() || (key.is_valid() && (event_key(key) == KEY_NONE || event_key(key) == KEY_UNKNOWN))) {
        return;
    }

    const auto replacement = normalize(raw, selected->event);
    const StringName id = selected->id;
    const Error changed = controls->replace_binding(id, replacement);
    if (changed != OK) {
        finish(changed == ERR_ALREADY_EXISTS ? result("Conflict", "That control is already assigned.") :
                                               result("InvalidBinding", "This control cannot be assigned to the selected slot."));
        return;
    }
    finish(result("Ok", "Binding changed.", slot_by_id(id)));
}

void Rebinding::on_timeout() {
    finish(result("Timeout", "No new control was pressed."));
}

void Rebinding::finish(const Ref<RebindResult> &outcome) {
    if (pending.is_null()) {
        return;
    }
    const Ref<Rebinding> keep_alive(this);
    const auto finished = std::exchange(pending, {});
    const auto installed_capture = std::exchange(capture, Callable());
    finished->outcome = outcome;
    const Callable callback = callable_mp(this, &Rebinding::on_timeout);
    if (deadline.is_valid() && deadline->is_connected("timeout", callback)) {
        deadline->disconnect("timeout", callback);
    }
    deadline.unref();
    selected.unref();
    modifier.unref();
    held_axes.clear();
    controls->end_capture(installed_capture);
    finished->emit_signal("completed", outcome);
    if (outcome->is_ok()) {
        emit_signal("changed", outcome->get_slot());
    }
}

Error Rebinding::cancel() {
    if (!on_main_thread()) {
        return ERR_BUSY;
    }
    if (pending.is_null()) {
        return ERR_DOES_NOT_EXIST;
    }
    finish(result("Cancelled", "Binding change cancelled."));
    return OK;
}

Ref<RebindResult> Rebinding::reset(const Ref<BindingSlot> &target) {
    if (!get_owner()) {
        return result("Unavailable", "Open settings with a live Controls scope.");
    }
    if (pending.is_valid() || controls->is_capturing()) {
        return result("Busy", "Finish the current binding change first.");
    }
    if (!owns(target)) {
        return result("InvalidSlot", "Could not restore this binding.");
    }
    const Error restored = controls->reset_binding(target->id);
    if (restored != OK) {
        return restored == ERR_ALREADY_EXISTS ? result("Conflict", "The default control is assigned to another slot.") :
                                               result("InvalidBinding", "Could not restore the default binding.");
    }
    const auto updated = slot_by_id(target->id);
    emit_signal("changed", updated);
    return result("Ok", "Default binding restored.", updated);
}

Ref<RebindResult> Rebinding::reset_all() {
    if (!get_owner()) {
        return result("Unavailable", "Open settings with a live Controls scope.");
    }
    if (pending.is_valid() || controls->is_capturing()) {
        return result("Busy", "Finish the current binding change first.");
    }
    controls->reset_bindings();
    emit_signal("changed", Ref<BindingSlot>());
    return result("Ok", "Default bindings restored.");
}

} // namespace crimson::rebinding
