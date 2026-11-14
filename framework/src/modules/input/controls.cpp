// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#include "controls.hpp"
#include "input_router.hpp"
#include "input_sources.hpp"

#include <godot_cpp/classes/display_server.hpp>
#include <godot_cpp/classes/input.hpp>
#include <godot_cpp/classes/os.hpp>
#include <godot_cpp/classes/window.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/callable_method_pointer.hpp>

#include <algorithm>
#include <set>

namespace crimson::input {
using namespace godot;

namespace {

// Scene-tree operations and the live device registry are main-thread only.
bool on_main_thread() {
    const OS *os = OS::get_singleton();
    return os && os->get_thread_caller_id() == os->get_main_thread_id();
}

// Return the public handle for the direct private router on this owner.
InputRouter *find_router(Node *owner) {
    for (int32_t i = 0; i < owner->get_child_count(true); ++i) {
        if (auto *router = Object::cast_to<InputRouter>(owner->get_child(i, true))) {
            if (!router->is_queued_for_deletion()) {
                return router;
            }
        }
    }
    return nullptr;
}

} // namespace

void Controls::_bind_methods() {
    ClassDB::bind_static_method("Controls", D_METHOD("boot", "owner", "layout", "joypad", "keyboard_and_mouse"), &Controls::boot, DEFVAL(Ref<InputLayout>()), DEFVAL(-1), DEFVAL(true));
    ClassDB::bind_static_method("Controls", D_METHOD("for_node", "node"), &Controls::for_node);
    ClassDB::bind_method(D_METHOD("is_ready"), &Controls::is_ready);
    ClassDB::bind_method(D_METHOD("get_error"), &Controls::get_error);
    ClassDB::bind_method(D_METHOD("get_owner"), &Controls::get_owner);
    ClassDB::bind_method(D_METHOD("get_layout"), &Controls::get_layout);
    ClassDB::bind_method(D_METHOD("has_action_kind", "id", "kind"), &Controls::has_action_kind);
    ClassDB::bind_method(D_METHOD("set_map_enabled", "map", "enabled"), &Controls::set_map_enabled);
    ClassDB::bind_method(D_METHOD("is_map_enabled", "map"), &Controls::is_map_enabled);
    ClassDB::bind_method(D_METHOD("pressed", "id"), &Controls::pressed);
    ClassDB::bind_method(D_METHOD("held", "id"), &Controls::held);
    ClassDB::bind_method(D_METHOD("released", "id"), &Controls::released);
    ClassDB::bind_method(D_METHOD("axis", "id"), &Controls::axis);
    ClassDB::bind_method(D_METHOD("vector", "id"), &Controls::vector);
    ClassDB::bind_method(D_METHOD("motion", "id"), &Controls::motion);
    ClassDB::bind_method(D_METHOD("on_press", "id", "callback"), &Controls::on_press);
    ClassDB::bind_method(D_METHOD("off_press", "id", "callback"), &Controls::off_press);
    ClassDB::bind_method(D_METHOD("capture_pointer"), &Controls::capture_pointer);
    ClassDB::bind_method(D_METHOD("release_pointer"), &Controls::release_pointer);
    ClassDB::bind_method(D_METHOD("pointer_captured"), &Controls::pointer_captured);
    ClassDB::bind_method(D_METHOD("set_pointer_available", "available"), &Controls::set_pointer_available);
    ClassDB::bind_method(D_METHOD("suspend_while_visible", "menu"), &Controls::suspend_while_visible);
    ClassDB::bind_method(D_METHOD("stop_suspending_while_visible", "menu"), &Controls::stop_suspending_while_visible);
    ClassDB::bind_method(D_METHOD("is_suspended"), &Controls::is_suspended);
    ClassDB::bind_method(D_METHOD("close"), &Controls::close);
    ADD_SIGNAL(MethodInfo("retired"));
    ADD_SIGNAL(MethodInfo("suspension_changed", PropertyInfo(Variant::BOOL, "suspended")));
}

InputRouter *Controls::resolve() const {
    if (!on_main_thread()) {
        return nullptr;
    }
    auto *router = Object::cast_to<InputRouter>(ObjectDB::get_instance(router_id));
    return router && !router->retired && router->is_inside_tree() && !router->is_queued_for_deletion() ? router : nullptr;
}

Ref<Controls> Controls::boot(Node *owner, const Ref<InputLayout> &layout, int32_t joypad, bool keyboard_and_mouse) {
    Ref<Controls> result;
    result.instantiate();
    if (!on_main_thread()) {
        result->error = ERR_UNAVAILABLE;
        return result;
    }
    if (!owner || !owner->is_inside_tree() || owner->is_queued_for_deletion() || joypad < -1 || (!keyboard_and_mouse && joypad < 0)) {
        result->error = ERR_INVALID_PARAMETER;
        return result;
    }
    if (InputRouter *existing = find_router(owner); existing && !existing->retired) {
        if (existing->layout == layout && existing->requested_gamepad == joypad && existing->use_keyboard_mouse == keyboard_and_mouse) {
            return existing->handle;
        }
        result->error = ERR_ALREADY_EXISTS;
        return result;
    }
    if (layout.is_valid()) {
        result->error = layout->validate();
        if (result->error != OK) {
            return result;
        }
    }
    for (auto it = InputRouter::live_scopes.begin(); it != InputRouter::live_scopes.end();) {
        const auto *scope = Object::cast_to<InputRouter>(ObjectDB::get_instance(*it));
        if (!scope || scope->retired || scope->is_queued_for_deletion()) {
            it = InputRouter::live_scopes.erase(it);
            continue;
        }
        if (scope->get_viewport() == owner->get_viewport() &&
                ((keyboard_and_mouse && scope->use_keyboard_mouse) || scope->requested_gamepad == joypad ||
                        (joypad >= 0 && scope->active_gamepad == joypad))) {
            result->error = ERR_ALREADY_IN_USE;
            return result;
        }
        ++it;
    }
    auto *router = memnew(InputRouter);
    router->handle = result;
    router->requested_gamepad = joypad;
    router->use_keyboard_mouse = keyboard_and_mouse;
    if (layout.is_valid()) {
        router->load_layout(layout);
    } else {
        router->load_project_actions();
    }
    result->router_id = ObjectID(router->get_instance_id());
    owner->add_child(router, false, Node::INTERNAL_MODE_BACK);
    if (!router->is_inside_tree()) {
        memdelete(router);
        result->error = ERR_CANT_CREATE;
        return result;
    }
    result->error = OK;
    InputRouter::live_scopes.insert(result->router_id);
    router->start();
    return result;
}

Ref<Controls> Controls::for_node(Node *node) {
    if (!on_main_thread()) {
        return {};
    }
    for (Node *owner = node; owner; owner = owner->get_parent()) {
        if (InputRouter *router = find_router(owner); router && !router->retired && router->is_inside_tree()) {
            return router->handle;
        }
    }
    return {};
}

bool Controls::is_ready() const { return resolve() != nullptr; }
Error Controls::get_error() const { return error; }

Node *Controls::get_owner() const {
    const InputRouter *router = resolve();
    return router ? router->get_parent() : nullptr;
}

Ref<InputLayout> Controls::get_layout() const {
    const InputRouter *router = resolve();
    return router ? router->layout : Ref<InputLayout>();
}

bool Controls::has_action_kind(const StringName &id, InputAction::ValueKind kind) const {
    const InputRouter *router = resolve();
    if (!router) {
        return false;
    }
    const auto found = router->actions.find(id);
    return found != router->actions.end() && found->second.kind == kind;
}

Error Controls::set_map_enabled(const StringName &map, bool enabled) {
    InputRouter *router = resolve();
    return router ? router->set_map_enabled(map, enabled) : ERR_UNCONFIGURED;
}

bool Controls::is_map_enabled(const StringName &map) const {
    const InputRouter *router = resolve();
    if (!router) {
        return false;
    }
    const auto found = router->maps.find(map);
    return found != router->maps.end() && found->second;
}

bool Controls::pressed(const StringName &id) const {
    const InputRouter *router = resolve();
    if (!router) {
        return false;
    }
    const auto action = router->actions.find(id);
    return action != router->actions.end() && action->second.frame_pressed;
}

bool Controls::held(const StringName &id) const {
    const InputRouter *router = resolve();
    if (!router) {
        return false;
    }
    const auto action = router->actions.find(id);
    return action != router->actions.end() && action->second.kind == InputAction::BUTTON && action->second.frame_held;
}

bool Controls::released(const StringName &id) const {
    const InputRouter *router = resolve();
    if (!router) {
        return false;
    }
    const auto action = router->actions.find(id);
    return action != router->actions.end() && action->second.frame_released;
}

double Controls::axis(const StringName &id) const {
    const InputRouter *router = resolve();
    if (!router) {
        return 0.0;
    }
    const auto action = router->actions.find(id);
    return action != router->actions.end() && action->second.kind == InputAction::AXIS ? action->second.frame_level.x : 0.0;
}

Vector2 Controls::vector(const StringName &id) const {
    const InputRouter *router = resolve();
    if (!router) {
        return {};
    }
    const auto action = router->actions.find(id);
    return action != router->actions.end() && action->second.kind == InputAction::VECTOR ? action->second.frame_level : Vector2();
}

Vector2 Controls::motion(const StringName &id) const {
    const InputRouter *router = resolve();
    if (!router) {
        return {};
    }
    const auto action = router->actions.find(id);
    return action != router->actions.end() && action->second.kind == InputAction::MOTION ? action->second.frame_motion : Vector2();
}

Error Controls::on_press(const StringName &id, const Callable &callback) {
    InputRouter *router = resolve();
    if (!router) {
        return ERR_UNCONFIGURED;
    }
    const auto action = router->actions.find(id);
    if (!callback.is_valid() || action == router->actions.end() || action->second.kind != InputAction::BUTTON) {
        return ERR_INVALID_PARAMETER;
    }
    auto &listeners = action->second.listeners;
    if (std::find(listeners.begin(), listeners.end(), callback) == listeners.end()) {
        listeners.push_back(callback);
    }
    return OK;
}

Error Controls::off_press(const StringName &id, const Callable &callback) {
    InputRouter *router = resolve();
    if (!router) {
        return ERR_UNCONFIGURED;
    }
    const auto action = router->actions.find(id);
    if (action == router->actions.end()) {
        return ERR_DOES_NOT_EXIST;
    }
    auto &listeners = action->second.listeners;
    listeners.erase(std::remove(listeners.begin(), listeners.end(), callback), listeners.end());
    return OK;
}

Error Controls::capture_pointer() {
    InputRouter *router = resolve();
    if (!router) {
        return ERR_UNCONFIGURED;
    }
    if (!router->use_keyboard_mouse || !router->pointer_available || !router->focused || router->suspended) {
        return ERR_UNAVAILABLE;
    }
    return router->capture_mouse();
}

Error Controls::release_pointer() {
    InputRouter *router = resolve();
    if (!router) {
        return ERR_UNCONFIGURED;
    }
    router->restore_pointer = false;
    if (router->owns_pointer()) {
        router->release_mouse();
        router->invalidate(true);
    }
    return OK;
}

bool Controls::pointer_captured() const {
    const InputRouter *router = resolve();
    return router && router->owns_pointer();
}

void Controls::set_pointer_available(bool available) {
    if (InputRouter *router = resolve(); router && router->pointer_available != available) {
        router->pointer_available = available;
        if (!available) {
            router->restore_pointer = false;
            router->release_mouse();
            router->invalidate(false);
        }
    }
}

Error Controls::suspend_while_visible(Control *menu) {
    InputRouter *router = resolve();
    if (!router) {
        return ERR_UNCONFIGURED;
    }
    if (!menu || !menu->is_inside_tree() || menu->get_viewport() != router->get_viewport()) {
        return ERR_INVALID_PARAMETER;
    }
    const uint64_t id = menu->get_instance_id();
    if (router->menus.emplace(ObjectID(id), menu->is_visible_in_tree()).second) {
        menu->connect("visibility_changed", callable_mp(router, &InputRouter::menu_changed).bind(id));
        menu->connect("tree_exiting", callable_mp(router, &InputRouter::menu_exiting).bind(id), CONNECT_ONE_SHOT);
        router->refresh_suspension();
    }
    return OK;
}

Error Controls::stop_suspending_while_visible(Control *menu) {
    InputRouter *router = resolve();
    if (!router) {
        return ERR_UNCONFIGURED;
    }
    if (!menu) {
        return ERR_INVALID_PARAMETER;
    }
    const uint64_t id = menu->get_instance_id();
    if (router->menus.erase(ObjectID(id)) > 0) {
        menu->disconnect("visibility_changed", callable_mp(router, &InputRouter::menu_changed).bind(id));
        menu->disconnect("tree_exiting", callable_mp(router, &InputRouter::menu_exiting).bind(id));
        router->refresh_suspension();
    }
    return OK;
}

bool Controls::is_suspended() const {
    const InputRouter *router = resolve();
    return !router || router->suspended;
}

void Controls::close() {
    if (InputRouter *router = resolve()) {
        router->queue_free();
        router->retire();
    }
}

TypedArray<InputBinding> Controls::bindings() const {
    TypedArray<InputBinding> result;
    if (const InputRouter *router = resolve()) {
        for (const StringName &id : router->slot_order) {
            result.push_back(router->copy_slot(id, false));
        }
    }
    return result;
}

Ref<InputBinding> Controls::binding(const StringName &id) const {
    const InputRouter *router = resolve();
    return router ? router->copy_slot(id, false) : Ref<InputBinding>();
}

Ref<InputBinding> Controls::default_binding(const StringName &id) const {
    const InputRouter *router = resolve();
    return router ? router->copy_slot(id, true) : Ref<InputBinding>();
}

Error Controls::replace_binding(const StringName &id, const Ref<InputEvent> &event) {
    if (!is_ready()) {
        return ERR_UNCONFIGURED;
    }
    Ref<InputBinding> snapshot = binding(id);
    if (snapshot.is_null()) {
        return ERR_DOES_NOT_EXIST;
    }
    snapshot->set_event(event);
    TypedArray<InputBinding> edits;
    edits.push_back(snapshot);
    return replace_bindings(edits);
}

Error Controls::replace_bindings(const TypedArray<InputBinding> &edits) {
    InputRouter *router = resolve();
    if (!router) {
        return ERR_UNCONFIGURED;
    }
    std::map<InputRouter::Binding *, Ref<InputEvent>> updates;
    std::set<StringName> touched;
    for (int64_t i = 0; i < edits.size(); ++i) {
        const Ref<InputBinding> edit = edits[i];
        if (edit.is_null() || !touched.insert(edit->get_id()).second) {
            return ERR_INVALID_PARAMETER;
        }
        const auto location = router->slot_locations.find(edit->get_id());
        if (location == router->slot_locations.end()) {
            return ERR_DOES_NOT_EXIST;
        }
        const auto &[action_id, index] = location->second;
        InputRouter::Action &action = router->actions.at(action_id);
        InputRouter::Binding &binding = action.bindings[index];
        if (binding.definition->get_stick() != InputBinding::NONE) {
            return ERR_UNAVAILABLE;
        }
        const Ref<InputEvent> event = edit->get_event();
        if (!valid_binding_event(event, action.kind)) {
            return ERR_INVALID_PARAMETER;
        }
        updates.emplace(&binding, copy_binding_event(event));
    }
    for (const auto &[edited, event] : updates) {
        for (auto &[id, action] : router->actions) {
            for (InputRouter::Binding &other : action.bindings) {
                if (&other == edited) {
                    continue;
                }
                const InputBinding::Stick stick = other.definition->get_stick();
                if (stick != InputBinding::NONE) {
                    if (stick_overlaps(stick, event) && !stick_overlaps(stick, edited->event)) {
                        return ERR_ALREADY_EXISTS;
                    }
                    continue;
                }
                const auto pending = updates.find(&other);
                const Ref<InputEvent> other_event = pending == updates.end() ? other.event : pending->second;
                if (!bindings_overlap(event, other_event)) {
                    continue;
                }
                const auto edited_default = edited->definition->get_event();
                const auto other_default = other.definition->get_event();
                if ((!bindings_overlap(event, edited->event) || !bindings_overlap(other_event, other.event)) &&
                        (!bindings_overlap(edited_default, other_default) || !bindings_overlap(event, edited_default) ||
                                !bindings_overlap(other_event, other_default))) {
                    return ERR_ALREADY_EXISTS;
                }
            }
        }
    }
    for (auto &[binding, event] : updates) {
        binding->event = event;
        binding->observed_strength = 0.0f;
        binding->wait_for_neutral = false;
        binding->neutral_sample = false;
    }
    if (!updates.empty()) {
        router->invalidate(false);
    }
    return OK;
}

Error Controls::reset_binding(const StringName &id) {
    const Ref<InputBinding> defaults = default_binding(id);
    if (defaults.is_valid() && defaults->get_stick() != InputBinding::NONE) {
        return OK;
    }
    return defaults.is_valid() ? replace_binding(id, defaults->get_event()) : is_ready() ? ERR_DOES_NOT_EXIST : ERR_UNCONFIGURED;
}

void Controls::reset_bindings() {
    if (InputRouter *router = resolve()) {
        for (auto &[id, action] : router->actions) {
            for (auto &slot : action.bindings) {
                if (slot.definition->get_stick() != InputBinding::NONE) {
                    continue;
                }
                slot.event = slot.definition->get_event();
                slot.observed_strength = 0.0f;
                slot.wait_for_neutral = false;
                slot.neutral_sample = false;
            }
        }
        router->invalidate(false);
    }
}

Error Controls::begin_capture(const Callable &callback, Node *owner) {
    InputRouter *router = resolve();
    if (!router) {
        return ERR_UNCONFIGURED;
    }
    if (!callback.is_valid() || (owner && (!owner->is_inside_tree() || owner->get_viewport() != router->get_viewport()))) {
        return ERR_INVALID_PARAMETER;
    }
    if (!router->focused) {
        return ERR_UNAVAILABLE;
    }
    if (router->capture_callback.is_valid() || router->capture_reserved()) {
        return ERR_BUSY;
    }
    router->capture_callback = callback;
    if (owner) {
        router->capture_owner = ObjectID(owner->get_instance_id());
        owner->connect("tree_exiting", callable_mp(router, &InputRouter::force_end_capture));
    }
    router->invalidate(false);
    router->refresh_suspension();
    return OK;
}

bool Controls::is_capturing() const {
    const InputRouter *router = resolve();
    return router && router->capture_callback.is_valid();
}

void Controls::end_capture(const Callable &callback) {
    if (InputRouter *router = resolve(); router && router->capture_callback == callback) {
        router->finish_capture(false);
    }
}

} // namespace crimson::input
