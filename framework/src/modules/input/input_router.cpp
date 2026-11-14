// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#include "input_router.hpp"
#include "input_sources.hpp"

#include <godot_cpp/classes/display_server.hpp>
#include <godot_cpp/classes/input.hpp>
#include <godot_cpp/classes/input_event_action.hpp>
#include <godot_cpp/classes/input_event_joypad_motion.hpp>
#include <godot_cpp/classes/input_event_mouse_button.hpp>
#include <godot_cpp/classes/input_event_mouse_motion.hpp>
#include <godot_cpp/classes/input_event_with_modifiers.hpp>
#include <godot_cpp/classes/input_map.hpp>
#include <godot_cpp/classes/scene_tree.hpp>
#include <godot_cpp/classes/window.hpp>
#include <godot_cpp/variant/callable_method_pointer.hpp>

#include <algorithm>
#include <cmath>

namespace crimson::input {
using namespace godot;

namespace {

// Only one desktop scope can own the OS pointer at a time.
ObjectID pointer_owner;

// Ignore minor analog drift when determining whether a source has returned to neutral.
constexpr float map_neutral_threshold = 0.05f;

// Whether the current DisplayServer offers a real window and desktop pointer.
bool has_desktop() {
    const DisplayServer *display = DisplayServer::get_singleton();
    return display && display->get_name() != "headless";
}

// Detect wheel buttons, which are complete click pulses without held state.
bool is_wheel(const Ref<InputEvent> &event) {
    const auto *button = Object::cast_to<InputEventMouseButton>(event.ptr());
    return button && button->get_button_index() >= MOUSE_BUTTON_WHEEL_UP && button->get_button_index() <= MOUSE_BUTTON_WHEEL_RIGHT;
}

// Read one action ID from an explicitly simulated Godot action, if present.
const InputEventAction *as_action(const Ref<InputEvent> &event) {
    return Object::cast_to<InputEventAction>(event.ptr());
}

// Let unmodified direction bindings keep working with unrelated held buttons.
bool match_modifiers(InputAction::ValueKind kind, const Ref<InputEvent> &binding) {
    const auto *modified = Object::cast_to<InputEventWithModifiers>(binding.ptr());
    return kind == InputAction::BUTTON || (modified && modified->get_modifiers_mask() != 0);
}

// Deep-copy authored modifier resources away from a runtime scope or an external snapshot.
TypedArray<InputModifier> copy_modifiers(const TypedArray<InputModifier> &source) {
    TypedArray<InputModifier> result;
    for (int64_t i = 0; i < source.size(); ++i) {
        const Ref<InputModifier> modifier = source[i];
        result.push_back(Ref<InputModifier>(modifier->duplicate()));
    }
    return result;
}

} // namespace

std::set<ObjectID> InputRouter::live_scopes;

void InputRouter::_bind_methods() {}

Vector2 InputRouter::Binding::modified(Vector2 value) const {
    if (!value.is_finite()) {
        return {};
    }
    for (const Ref<InputModifier> &modifier : modifiers) {
        value = modifier->apply(value);
        if (!value.is_finite()) {
            return {};
        }
    }
    return value;
}

void InputRouter::Binding::update_stick(JoyAxis axis, float value, bool raw, bool eligible) {
    const int component = stick_component(definition->get_stick(), axis);
    if (component < 0 || !std::isfinite(value)) {
        return;
    }
    value = std::clamp(value, -1.0f, 1.0f);
    if (raw) {
        observed_stick[component] = value;
        observed_strength = observed_stick.length();
        neutral_sample = wait_for_neutral && observed_strength <= map_neutral_threshold;
        if (neutral_sample) {
            wait_for_neutral = false;
        }
    }
    if (wait_for_neutral || neutral_sample) {
        return;
    }
    real_t &previous = stick_value[component];
    if (eligible) {
        previous = value;
    } else if (previous * value <= 0.0f) {
        previous = 0.0f;
    } else if (std::abs(value) < std::abs(previous)) {
        previous = value;
    }
}

void InputRouter::append_binding(const StringName &action_id, const Ref<InputBinding> &source) {
    Ref<InputBinding> definition;
    definition.instantiate();
    definition->set_id(source->get_id());
    definition->set_action(action_id);
    definition->set_display_name(source->get_display_name());
    definition->set_contribution(source->get_contribution());
    definition->set_event(copy_binding_event(source->get_event()));
    definition->set_stick(source->get_stick());
    definition->set_mouse_axes(source->get_mouse_axes());
    const auto modifiers = copy_modifiers(source->get_modifiers());
    definition->set_modifiers(modifiers);
    Action &action = actions.at(action_id);
    slot_locations.emplace(source->get_id(), std::make_pair(action_id, action.bindings.size()));
    slot_order.push_back(source->get_id());
    Binding binding;
    binding.definition = definition;
    binding.event = definition->get_event();
    binding.modifiers.reserve(modifiers.size());
    for (int64_t i = 0; i < modifiers.size(); ++i) {
        binding.modifiers.emplace_back(Ref<InputModifier>(modifiers[i]));
    }
    action.bindings.push_back(std::move(binding));
}

void InputRouter::load_layout(const Ref<InputLayout> &source) {
    layout = source;
    click_to_capture = source->get_click_to_capture();
    release_action = source->get_release_action();
    const auto authored_maps = source->get_maps();
    for (int64_t i = 0; i < authored_maps.size(); ++i) {
        const Ref<InputActionMap> map = authored_maps[i];
        maps.try_emplace(map->get_id(), false);
    }
    const TypedArray<InputAction> definitions = source->resolved_actions();
    for (int64_t i = 0; i < definitions.size(); ++i) {
        const Ref<InputAction> action = definitions[i];
        const StringName id = action->get_id();
        Action &state = actions[id];
        state.kind = action->get_kind();
        state.map = action->get_map();
        state.enabled = maps.try_emplace(state.map, false).first->second;
        state.deadzone = action->get_deadzone();
        state.requires_pointer = action->get_requires_pointer();
        action_order.push_back(id);
        const TypedArray<InputBinding> bindings = action->get_bindings();
        for (int64_t j = 0; j < bindings.size(); ++j) {
            append_binding(id, bindings[j]);
        }
    }
}

void InputRouter::load_project_actions() {
    InputMap *input_map = InputMap::get_singleton();
    const TypedArray<StringName> ids = input_map->get_actions();
    for (int64_t i = 0; i < ids.size(); ++i) {
        const StringName id = ids[i];
        actions[id].deadzone = input_map->action_get_deadzone(id);
        action_order.push_back(id);
        const auto events = input_map->action_get_events(id);
        for (int64_t j = 0; j < events.size(); ++j) {
            const Ref<InputEvent> event = events[j];
            if (event.is_null() || !valid_binding_event(event, InputAction::BUTTON)) {
                continue;
            }
            Ref<InputBinding> definition;
            definition.instantiate();
            definition->set_id(StringName(String(id) + "/" + String::num_int64(j)));
            definition->set_display_name(String(id));
            definition->set_event(event);
            append_binding(id, definition);
        }
    }
}

Error InputRouter::set_map_enabled(const StringName &map, bool enabled) {
    const auto found = maps.find(map);
    if (found == maps.end()) {
        return ERR_DOES_NOT_EXIST;
    }
    if (found->second == enabled) {
        return OK;
    }
    found->second = enabled;
    for (auto &[id, action] : actions) {
        if (action.map != map) {
            continue;
        }
        action.enabled = enabled;
        ++action.map_generation;
        invalidate_action(action, false);
        action.synthetic_wait_for_release = action.synthetic_observed;
        for (Binding &binding : action.bindings) {
            binding.wait_for_neutral = binding.observed_strength > map_neutral_threshold;
        }
    }
    return OK;
}

bool InputRouter::button_down(const Action &action) const {
    if (action.synthetic_held) {
        return true;
    }
    return std::any_of(action.bindings.begin(), action.bindings.end(), [&](const Binding &binding) {
        return binding.strength > action.deadzone;
    });
}

Vector2 InputRouter::read_level(const Action &action) const {
    Vector2 positive;
    Vector2 negative;
    for (const Binding &binding : action.bindings) {
        const Vector3 contribution = binding.definition->get_contribution();
        Vector2 value = binding.definition->get_stick() != InputBinding::NONE ?
                binding.stick_value : Vector2(contribution.x, contribution.y) * binding.strength;
        if (!binding.modifiers.empty()) {
            // A scalar modifier measures only X. Leave legacy unmodified contributions untouched.
            if (action.kind == InputAction::AXIS) {
                value.y = 0.0f;
            }
            value = binding.modified(value);
        }
        positive.x = std::max(positive.x, value.x);
        positive.y = std::max(positive.y, value.y);
        negative.x = std::min(negative.x, value.x);
        negative.y = std::min(negative.y, value.y);
    }
    const Vector2 level = positive + negative;
    const float length = level.length();
    const double magnitude = std::isfinite(length) ? length :
            std::hypot(static_cast<double>(level.x), static_cast<double>(level.y));
    if (magnitude <= action.deadzone) {
        return {};
    }
    const double normalized = (std::min(1.0, magnitude) - action.deadzone) / (1.0 - action.deadzone);
    return level * static_cast<real_t>(normalized / magnitude);
}

bool InputRouter::accepts_action(const Action &action) const {
    return action.enabled && !suspended && (!action.requires_pointer || owns_pointer());
}

void InputRouter::record_button(const StringName &id, Action &action, bool previous, bool pulse) {
    const bool current = button_down(action);
    if (accepts_action(action) && (pulse || (!previous && current))) {
        ++action.pending_presses;
        if (id == release_action && owns_pointer()) {
            handle->release_pointer();
            return;
        }
    }
    if (accepts_action(action) && (pulse || (previous && !current))) {
        action.pending_release = true;
    }
}

void InputRouter::update_actions(const Ref<InputEvent> &event, bool admit) {
    const auto *synthetic = as_action(event);
    const auto *mouse = Object::cast_to<InputEventMouseMotion>(event.ptr());
    const auto *axis = Object::cast_to<InputEventJoypadMotion>(event.ptr());
    const bool pulse = is_wheel(event) && event->is_pressed();
    for (const StringName &id : action_order) {
        Action &action = actions.at(id);
        const bool eligible = admit && accepts_action(action);
        if (mouse) {
            if (!eligible || action.kind != InputAction::MOTION) {
                continue;
            }
            for (const Binding &binding : action.bindings) {
                if (binding.event.is_valid()) {
                    const Vector3 scale = binding.definition->get_contribution();
                    Vector2 motion = mouse->get_screen_relative();
                    if (motion.is_finite()) {
                        switch (binding.definition->get_mouse_axes()) {
                            case InputBinding::MOUSE_X:
                                motion.y = 0;
                                break;
                            case InputBinding::MOUSE_Y:
                                motion.x = 0;
                                break;
                            default:
                                break;
                        }
                        const Vector2 next = action.pending_motion + binding.modified(motion * Vector2(scale.x, scale.y));
                        if (next.is_finite()) {
                            action.pending_motion = next;
                        }
                    }
                }
            }
            continue;
        }
        const bool previous = action.kind == InputAction::BUTTON && button_down(action);
        bool matched_pulse = false;
        if (synthetic && action.kind == InputAction::BUTTON && synthetic->get_action() == id) {
            action.synthetic_observed = event->is_pressed();
            if (!action.synthetic_observed) {
                action.synthetic_wait_for_release = false;
            }
            if ((eligible && !action.synthetic_wait_for_release) || !event->is_pressed()) {
                action.synthetic_held = event->is_pressed();
            }
        } else {
            for (Binding &binding : action.bindings) {
                if (binding.definition->get_stick() != InputBinding::NONE) {
                    if (axis) {
                        binding.update_stick(axis->get_axis(), axis->get_axis_value(), !admit, eligible);
                    }
                    continue;
                }
                const float strength = binding_strength(binding.event, event, eligible && match_modifiers(action.kind, binding.event));
                // Only the raw pass updates observed levels. Ignore wheel pulses, which have no release event.
                if (!admit && !pulse && strength >= 0.0f) {
                    // The two signed bindings of one stick axis share its physical neutral position.
                    binding.observed_strength = axis ? std::abs(axis->get_axis_value()) : strength;
                    binding.neutral_sample = binding.wait_for_neutral &&
                            binding.observed_strength <= map_neutral_threshold;
                    if (binding.neutral_sample) {
                        binding.wait_for_neutral = false;
                    }
                }
                if (binding.wait_for_neutral || (binding.neutral_sample && !pulse)) {
                    continue;
                }
                if (eligible && pulse && strength > 0.0f) {
                    matched_pulse = true;
                } else if (strength >= 0.0f && ((eligible && !event->is_echo()) || strength < binding.strength)) {
                    binding.strength = strength;
                }
            }
        }
        if (action.kind == InputAction::BUTTON && !event->is_echo()) {
            record_button(id, action, previous, matched_pulse);
        }
    }
}

bool InputRouter::accepts_device(const Ref<InputEvent> &event) {
    if (event.is_null()) {
        return false;
    }
    if (!is_gamepad(event)) {
        return use_keyboard_mouse;
    }
    if (requested_gamepad >= 0) {
        return event->get_device() == requested_gamepad;
    }
    if (active_gamepad < 0) {
        const Array devices = Input::get_singleton()->get_connected_joypads();
        for (int64_t i = 0; i < devices.size(); ++i) {
            const int32_t device = devices[i];
            if (!gamepad_reserved(device)) {
                active_gamepad = device;
                break;
            }
        }
        if (active_gamepad < 0 && devices.is_empty() && !gamepad_reserved(event->get_device())) {
            active_gamepad = event->get_device();
        }
    }
    return active_gamepad >= 0 && event->get_device() == active_gamepad;
}

bool InputRouter::capture_reserved() const {
    const ObjectID self(get_instance_id());
    for (const ObjectID &id : live_scopes) {
        if (id == self) {
            continue;
        }
        const auto *scope = Object::cast_to<InputRouter>(ObjectDB::get_instance(id));
        if (scope && !scope->retired && scope->get_viewport() == get_viewport() && scope->capture_callback.is_valid()) {
            return true;
        }
    }
    return false;
}

bool InputRouter::gamepad_reserved(int32_t device) const {
    const ObjectID self(get_instance_id());
    for (const ObjectID &id : live_scopes) {
        if (id == self) {
            continue;
        }
        const auto *scope = Object::cast_to<InputRouter>(ObjectDB::get_instance(id));
        if (scope && !scope->retired && scope->get_viewport() == get_viewport() &&
                (scope->requested_gamepad == device || scope->active_gamepad == device)) {
            return true;
        }
    }
    return false;
}

void InputRouter::invalidate(bool preserve_levels) {
    ++generation;
    for (auto &[id, action] : actions) {
        invalidate_action(action, preserve_levels);
    }
}

void InputRouter::invalidate_action(Action &action, bool preserve_levels) {
    action.pending_presses = 0;
    action.pending_release = false;
    action.frame_pressed = false;
    action.frame_released = false;
    action.frame_motion = {};
    action.pending_motion = {};
    action.frame_held = false;
    action.synthetic_held = false;
    if (!preserve_levels || action.kind == InputAction::BUTTON) {
        for (Binding &binding : action.bindings) {
            binding.strength = 0.0f;
            binding.stick_value = {};
        }
    }
    action.frame_level = preserve_levels && accepts_action(action) ? read_level(action) : Vector2();
}

void InputRouter::forget_observed_input(bool gamepad_only) {
    for (auto &[id, action] : actions) {
        if (!gamepad_only) {
            action.synthetic_observed = false;
            action.synthetic_wait_for_release = false;
        }
        for (Binding &binding : action.bindings) {
            if (!gamepad_only || is_gamepad(binding.event) || binding.definition->get_stick() != InputBinding::NONE) {
                binding.observed_strength = 0.0f;
                binding.observed_stick = {};
                binding.wait_for_neutral = false;
                binding.neutral_sample = false;
            }
        }
    }
}

void InputRouter::publish() {
    const uint64_t stamp = generation;
    // Store the action's map revision alongside each queued batch of press callbacks.
    struct PublishedPresses {
        StringName id; ///< Action owning these callbacks.
        uint32_t count; ///< Number of press edges to dispatch.
        uint64_t map_generation; ///< Revision captured at publication.
    };
    std::vector<PublishedPresses> events;
    for (const StringName &id : action_order) {
        Action &action = actions.at(id);
        const bool eligible = accepts_action(action);
        action.frame_pressed = eligible && action.pending_presses > 0;
        action.frame_released = eligible && action.pending_release;
        action.frame_held = eligible && button_down(action);
        action.frame_motion = eligible ? action.pending_motion : Vector2();
        action.frame_level = eligible ? read_level(action) : Vector2();
        if (action.frame_pressed) {
            events.push_back({id, action.pending_presses, action.map_generation});
        }
        action.pending_presses = 0;
        action.pending_release = false;
        action.pending_motion = {};
    }
    for (const auto &[id, count, map_stamp] : events) {
        const Action &action = actions.at(id);
        const auto listeners = action.listeners;
        for (uint32_t press = 0; press < count && action.map_generation == map_stamp; ++press) {
            for (const Callable &callback : listeners) {
                if (generation != stamp || retired) {
                    return;
                }
                if (action.map_generation != map_stamp) {
                    break;
                }
                const auto &current = action.listeners;
                if (callback.is_valid() && std::find(current.begin(), current.end(), callback) != current.end()) {
                    callback.call();
                }
            }
        }
    }
}

void InputRouter::start() {
    set_process_mode(PROCESS_MODE_ALWAYS);
    set_process_input(true);
    set_process_unhandled_input(true);
    get_tree()->connect("physics_frame", callable_mp(this, &InputRouter::publish));
    if (Window *window = get_window()) {
        focused = !has_desktop() || window->has_focus();
        window->connect("focus_entered", callable_mp(this, &InputRouter::focus_entered));
        window->connect("focus_exited", callable_mp(this, &InputRouter::focus_exited));
    }
    Input::get_singleton()->connect("joy_connection_changed", callable_mp(this, &InputRouter::joy_connection_changed));
    refresh_suspension();
}

void InputRouter::retire() {
    if (retired) {
        return;
    }
    retired = true;
    live_scopes.erase(ObjectID(get_instance_id()));
    set_process_input(false);
    set_process_unhandled_input(false);
    release_mouse();
    finish_capture(true);
    invalidate(false);
    handle->emit_signal("retired");
    handle.unref();
}

void InputRouter::_notification(int what) {
    if (what == NOTIFICATION_EXIT_TREE) {
        retire();
    }
}

void InputRouter::focus_exited() {
    focused = false;
    restore_pointer = false;
    recapture_button_down = false;
    release_mouse();
    finish_capture(true);
    refresh_suspension();
    invalidate(false);
    forget_observed_input(false);
}

void InputRouter::focus_entered() {
    focused = true;
    refresh_suspension();
}

void InputRouter::joy_connection_changed(int64_t device, bool connected) {
    if (!connected && (device == requested_gamepad || device == active_gamepad)) {
        active_gamepad = -1;
        invalidate(false);
        forget_observed_input(true);
        finish_capture(true);
    }
}

void InputRouter::refresh_suspension() {
    const bool next = !focused || capture_callback.is_valid() || std::any_of(menus.begin(), menus.end(), [](const auto &menu) {
        return menu.second;
    });
    if (next == suspended || retired) {
        return;
    }
    suspended = next;
    if (suspended) {
        restore_pointer = focused && owns_pointer();
        release_mouse();
    } else if (restore_pointer) {
        restore_pointer = false;
        handle->capture_pointer();
    }
    invalidate(false);
    handle->emit_signal("suspension_changed", suspended);
}

void InputRouter::menu_changed(uint64_t instance_id) {
    const ObjectID id(instance_id);
    auto entry = menus.find(id);
    if (entry != menus.end()) {
        const auto *menu = Object::cast_to<Control>(ObjectDB::get_instance(id));
        entry->second = menu && menu->is_visible_in_tree();
        refresh_suspension();
    }
}

void InputRouter::menu_exiting(uint64_t instance_id) {
    const ObjectID id(instance_id);
    if (menus.erase(id) != 0) {
        if (auto *menu = Object::cast_to<Control>(ObjectDB::get_instance(id))) {
            menu->disconnect("visibility_changed", callable_mp(this, &InputRouter::menu_changed).bind(instance_id));
        }
        refresh_suspension();
    }
}

void InputRouter::force_end_capture() {
    finish_capture(true);
}

void InputRouter::finish_capture(bool notify) {
    if (capture_callback.is_null()) {
        return;
    }
    if (auto *owner = Object::cast_to<Node>(ObjectDB::get_instance(capture_owner))) {
        const Callable callback = callable_mp(this, &InputRouter::force_end_capture);
        if (owner->is_connected("tree_exiting", callback)) {
            owner->disconnect("tree_exiting", callback);
        }
    }
    const Callable callback = capture_callback;
    capture_owner = ObjectID();
    capture_callback = Callable();
    refresh_suspension();
    if (notify && callback.is_valid()) {
        callback.call(Variant());
    }
}

Ref<InputBinding> InputRouter::copy_slot(const StringName &id, bool defaults) const {
    const auto location = slot_locations.find(id);
    if (location == slot_locations.end()) {
        return {};
    }
    const auto &[action_id, index] = location->second;
    const Binding &source = actions.at(action_id).bindings[index];
    Ref<InputBinding> result;
    result.instantiate();
    result->set_id(id);
    result->set_action(action_id);
    result->set_display_name(source.definition->get_display_name());
    result->set_contribution(source.definition->get_contribution());
    result->set_event(copy_binding_event(defaults ? source.definition->get_event() : source.event));
    result->set_stick(source.definition->get_stick());
    result->set_mouse_axes(source.definition->get_mouse_axes());
    result->set_modifiers(copy_modifiers(source.definition->get_modifiers()));
    return result;
}

bool InputRouter::owns_pointer() const {
    return pointer_owner == ObjectID(get_instance_id()) && Input::get_singleton()->get_mouse_mode() == Input::MOUSE_MODE_CAPTURED;
}

Error InputRouter::capture_mouse() {
    if (!has_desktop()) {
        return ERR_UNAVAILABLE;
    }
    const ObjectID id(get_instance_id());
    if (pointer_owner == id && owns_pointer()) {
        return OK;
    }
    if (pointer_owner.is_valid() && pointer_owner != id && ObjectDB::get_instance(pointer_owner)) {
        return ERR_ALREADY_IN_USE;
    }
    Input::get_singleton()->set_mouse_mode(Input::MOUSE_MODE_CAPTURED);
    if (Input::get_singleton()->get_mouse_mode() != Input::MOUSE_MODE_CAPTURED) {
        return ERR_UNAVAILABLE;
    }
    pointer_owner = id;
    invalidate(true);
    return OK;
}

void InputRouter::release_mouse() {
    if (pointer_owner != ObjectID(get_instance_id())) {
        return;
    }
    if (owns_pointer()) {
        Input::get_singleton()->set_mouse_mode(Input::MOUSE_MODE_VISIBLE);
    }
    pointer_owner = ObjectID();
}

void InputRouter::_input(const Ref<InputEvent> &event) {
    if (retired || !accepts_device(event)) {
        return;
    }
    if (const auto *mouse = Object::cast_to<InputEventMouseButton>(event.ptr())) {
        if (recapture_button_down && mouse->get_button_index() == MOUSE_BUTTON_LEFT && !mouse->is_pressed()) {
            recapture_button_down = false;
            update_actions(event, false);
            get_viewport()->set_input_as_handled();
            return;
        }
    }
    if (capture_callback.is_valid() && !Object::cast_to<InputEventMouseMotion>(event.ptr())) {
        const Callable callback = capture_callback;
        update_actions(event, false);
        get_viewport()->set_input_as_handled();
        callback.call(event);
        return;
    }
    update_actions(event, false);
}

void InputRouter::_unhandled_input(const Ref<InputEvent> &event) {
    if (retired || suspended || !accepts_device(event)) {
        return;
    }
    if (click_to_capture && pointer_available && !owns_pointer()) {
        if (const auto *mouse = Object::cast_to<InputEventMouseButton>(event.ptr())) {
            if (mouse->get_button_index() == MOUSE_BUTTON_LEFT && mouse->is_pressed() && handle->capture_pointer() == OK) {
                recapture_button_down = true;
                get_viewport()->set_input_as_handled();
                return;
            }
        }
    }
    if (recapture_button_down) {
        if (const auto *mouse = Object::cast_to<InputEventMouseButton>(event.ptr()); mouse && mouse->get_button_index() == MOUSE_BUTTON_LEFT) {
            get_viewport()->set_input_as_handled();
            return;
        }
    }
    update_actions(event, true);
}

} // namespace crimson::input
