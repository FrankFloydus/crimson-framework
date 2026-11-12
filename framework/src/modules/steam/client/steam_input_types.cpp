// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#include "modules/steam/client/steam_input_types.hpp"

#include <godot_cpp/core/class_db.hpp>

#include <utility>

namespace crimson::steam {
using namespace godot;

namespace {

// Make DTO properties script-readable without persisting instance state.
constexpr uint32_t read_only = PROPERTY_USAGE_NO_INSTANCE_STATE | PROPERTY_USAGE_READ_ONLY;

} // namespace

void SteamDigitalInput::_bind_methods() {
    ClassDB::bind_method(D_METHOD("is_active"), &SteamDigitalInput::is_active);
    ClassDB::bind_method(D_METHOD("is_pressed"), &SteamDigitalInput::is_pressed);
    ADD_PROPERTY(PropertyInfo(Variant::BOOL, "active", PROPERTY_HINT_NONE, "", read_only), "", "is_active");
    ADD_PROPERTY(PropertyInfo(Variant::BOOL, "pressed", PROPERTY_HINT_NONE, "", read_only), "", "is_pressed");
}

bool SteamDigitalInput::is_active() const {
    return active;
}

bool SteamDigitalInput::is_pressed() const {
    return pressed;
}

void SteamAnalogInput::_bind_methods() {
    ClassDB::bind_method(D_METHOD("is_active"), &SteamAnalogInput::is_active);
    ClassDB::bind_method(D_METHOD("get_value"), &SteamAnalogInput::get_value);
    ClassDB::bind_method(D_METHOD("get_mode"), &SteamAnalogInput::get_mode);
    ADD_PROPERTY(PropertyInfo(Variant::BOOL, "active", PROPERTY_HINT_NONE, "", read_only), "", "is_active");
    ADD_PROPERTY(PropertyInfo(Variant::VECTOR2, "value", PROPERTY_HINT_NONE, "", read_only), "", "get_value");
    PropertyInfo mode_info = GetTypeInfo<Mode>::get_class_info();
    mode_info.name = "mode";
    mode_info.usage |= read_only;
    ADD_PROPERTY(mode_info, "", "get_mode");

    BIND_ENUM_CONSTANT(MODE_UNKNOWN);
    BIND_ENUM_CONSTANT(MODE_NONE);
    BIND_ENUM_CONSTANT(MODE_DPAD);
    BIND_ENUM_CONSTANT(MODE_BUTTONS);
    BIND_ENUM_CONSTANT(MODE_FOUR_BUTTONS);
    BIND_ENUM_CONSTANT(MODE_ABSOLUTE_MOUSE);
    BIND_ENUM_CONSTANT(MODE_RELATIVE_MOUSE);
    BIND_ENUM_CONSTANT(MODE_JOYSTICK_MOVE);
    BIND_ENUM_CONSTANT(MODE_JOYSTICK_MOUSE);
    BIND_ENUM_CONSTANT(MODE_JOYSTICK_CAMERA);
    BIND_ENUM_CONSTANT(MODE_SCROLL_WHEEL);
    BIND_ENUM_CONSTANT(MODE_TRIGGER);
    BIND_ENUM_CONSTANT(MODE_TOUCH_MENU);
    BIND_ENUM_CONSTANT(MODE_MOUSE_JOYSTICK);
    BIND_ENUM_CONSTANT(MODE_MOUSE_REGION);
    BIND_ENUM_CONSTANT(MODE_RADIAL_MENU);
    BIND_ENUM_CONSTANT(MODE_SINGLE_BUTTON);
    BIND_ENUM_CONSTANT(MODE_SWITCHES);
}

bool SteamAnalogInput::is_active() const {
    return active;
}

Vector2 SteamAnalogInput::get_value() const {
    return value;
}

SteamAnalogInput::Mode SteamAnalogInput::get_mode() const {
    return mode;
}

void SteamDigitalInputResult::_bind_methods() {
    ClassDB::bind_method(D_METHOD("get_value"), &SteamDigitalInputResult::get_value);
    ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "value", PROPERTY_HINT_RESOURCE_TYPE,
                         SteamDigitalInput::get_class_static(), read_only), "", "get_value");
}

Ref<SteamDigitalInput> SteamDigitalInputResult::get_value() const {
    return value;
}

Ref<SteamDigitalInputResult> SteamDigitalInputResult::create(Error error, Ref<SteamDigitalInput> payload) {
    Ref<SteamDigitalInputResult> result;
    result.instantiate();
    result->set_outcome(error);
    result->value = std::move(payload);
    return result;
}

void SteamAnalogInputResult::_bind_methods() {
    ClassDB::bind_method(D_METHOD("get_value"), &SteamAnalogInputResult::get_value);
    ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "value", PROPERTY_HINT_RESOURCE_TYPE,
                         SteamAnalogInput::get_class_static(), read_only), "", "get_value");
}

Ref<SteamAnalogInput> SteamAnalogInputResult::get_value() const {
    return value;
}

Ref<SteamAnalogInputResult> SteamAnalogInputResult::create(Error error, Ref<SteamAnalogInput> payload) {
    Ref<SteamAnalogInputResult> result;
    result.instantiate();
    result->set_outcome(error);
    result->value = std::move(payload);
    return result;
}

void SteamTextureResult::_bind_methods() {
    ClassDB::bind_method(D_METHOD("get_value"), &SteamTextureResult::get_value);
    ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "value", PROPERTY_HINT_RESOURCE_TYPE,
                         Texture2D::get_class_static(), read_only), "", "get_value");
}

Ref<Texture2D> SteamTextureResult::get_value() const {
    return value;
}

Ref<SteamTextureResult> SteamTextureResult::create(Error error, Ref<Texture2D> payload) {
    Ref<SteamTextureResult> result;
    result.instantiate();
    result->set_outcome(error);
    result->value = std::move(payload);
    return result;
}

SteamTexturesResult::SteamTexturesResult() {
    value.make_read_only();
}

void SteamTexturesResult::_bind_methods() {
    ClassDB::bind_method(D_METHOD("get_value"), &SteamTexturesResult::get_value);
    ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "value", PROPERTY_HINT_ARRAY_TYPE,
                         Texture2D::get_class_static(), read_only), "", "get_value");
}

TypedArray<Texture2D> SteamTexturesResult::get_value() const {
    return value;
}

Ref<SteamTexturesResult> SteamTexturesResult::create(Error error, TypedArray<Texture2D> payload) {
    Ref<SteamTexturesResult> result;
    result.instantiate();
    result->set_outcome(error);
    payload.make_read_only();
    result->value = std::move(payload);
    return result;
}

} // namespace crimson::steam
