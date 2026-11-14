// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#include "input_modifiers.hpp"
#include "controls.hpp"

#include <godot_cpp/core/class_db.hpp>

#include <algorithm>
#include <cmath>

namespace crimson::input {
using namespace godot;

namespace {

// Remap an absolute magnitude through the valid authored thresholds.
real_t remap_magnitude(double magnitude, double lower, double upper) {
    return static_cast<real_t>(std::clamp((magnitude - lower) / (upper - lower), 0.0, 1.0));
}

// Measure component length without overflowing for large finite authored gains.
double precise_length(const Vector2 &value) {
    return std::hypot(static_cast<double>(value.x), static_cast<double>(value.y));
}

// Remap a normalized component while retaining its original sign.
real_t curve_component(real_t value, double exponent) {
    const double magnitude = std::min(std::abs(static_cast<double>(value)), 1.0);
    return static_cast<real_t>(std::copysign(std::pow(magnitude, exponent), value));
}

} // namespace

void InputModifier::_bind_methods() {}

void InputDeadZone::_bind_methods() {
    ClassDB::bind_method(D_METHOD("set_mode", "value"), &InputDeadZone::set_mode);
    ClassDB::bind_method(D_METHOD("get_mode"), &InputDeadZone::get_mode);
    ClassDB::bind_method(D_METHOD("set_lower_threshold", "value"), &InputDeadZone::set_lower_threshold);
    ClassDB::bind_method(D_METHOD("get_lower_threshold"), &InputDeadZone::get_lower_threshold);
    ClassDB::bind_method(D_METHOD("set_upper_threshold", "value"), &InputDeadZone::set_upper_threshold);
    ClassDB::bind_method(D_METHOD("get_upper_threshold"), &InputDeadZone::get_upper_threshold);
    BIND_ENUM_CONSTANT(RADIAL);
    BIND_ENUM_CONSTANT(AXIAL);
    ADD_PROPERTY(PropertyInfo(Variant::INT, "mode", PROPERTY_HINT_ENUM, "Radial,Axial"), "set_mode", "get_mode");
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "lower_threshold", PROPERTY_HINT_RANGE, "0,1,0.01"), "set_lower_threshold", "get_lower_threshold");
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "upper_threshold", PROPERTY_HINT_RANGE, "0,1,0.01"), "set_upper_threshold", "get_upper_threshold");
}

void InputDeadZone::set_mode(Mode value) { mode = value; emit_changed(); }
InputDeadZone::Mode InputDeadZone::get_mode() const { return mode; }
void InputDeadZone::set_lower_threshold(double value) { lower_threshold = value; emit_changed(); }
double InputDeadZone::get_lower_threshold() const { return lower_threshold; }
void InputDeadZone::set_upper_threshold(double value) { upper_threshold = value; emit_changed(); }
double InputDeadZone::get_upper_threshold() const { return upper_threshold; }

bool InputDeadZone::supports(int kind) const {
    return (kind == InputAction::AXIS || kind == InputAction::VECTOR) && (mode == RADIAL || mode == AXIAL) &&
            std::isfinite(lower_threshold) && std::isfinite(upper_threshold) &&
            lower_threshold >= 0.0 && lower_threshold < upper_threshold && upper_threshold <= 1.0;
}

Vector2 InputDeadZone::apply(const Vector2 &value) const {
    if (mode == AXIAL) {
        return {
            std::copysign(remap_magnitude(std::abs(value.x), lower_threshold, upper_threshold), value.x),
            std::copysign(remap_magnitude(std::abs(value.y), lower_threshold, upper_threshold), value.y)
        };
    }
    const double length = precise_length(value);
    return length > lower_threshold ?
            value * static_cast<real_t>(remap_magnitude(length, lower_threshold, upper_threshold) / length) : Vector2();
}

void InputSwapAxis::_bind_methods() {
    ClassDB::bind_method(D_METHOD("set_order", "value"), &InputSwapAxis::set_order);
    ClassDB::bind_method(D_METHOD("get_order"), &InputSwapAxis::get_order);
    BIND_ENUM_CONSTANT(XY);
    BIND_ENUM_CONSTANT(YX);
    ADD_PROPERTY(PropertyInfo(Variant::INT, "order", PROPERTY_HINT_ENUM, "X then Y,Y then X"), "set_order", "get_order");
}

void InputSwapAxis::set_order(Order value) { order = value; emit_changed(); }
InputSwapAxis::Order InputSwapAxis::get_order() const { return order; }

bool InputSwapAxis::supports(int kind) const {
    return (kind == InputAction::VECTOR || kind == InputAction::MOTION) && (order == XY || order == YX);
}

Vector2 InputSwapAxis::apply(const Vector2 &value) const {
    return order == YX ? Vector2(value.y, value.x) : value;
}

void InputInvert::_bind_methods() {
    ClassDB::bind_method(D_METHOD("set_x", "value"), &InputInvert::set_x);
    ClassDB::bind_method(D_METHOD("get_x"), &InputInvert::get_x);
    ClassDB::bind_method(D_METHOD("set_y", "value"), &InputInvert::set_y);
    ClassDB::bind_method(D_METHOD("get_y"), &InputInvert::get_y);
    ADD_PROPERTY(PropertyInfo(Variant::BOOL, "x"), "set_x", "get_x");
    ADD_PROPERTY(PropertyInfo(Variant::BOOL, "y"), "set_y", "get_y");
}

void InputInvert::set_x(bool value) { x = value; emit_changed(); }
bool InputInvert::get_x() const { return x; }
void InputInvert::set_y(bool value) { y = value; emit_changed(); }
bool InputInvert::get_y() const { return y; }

bool InputInvert::supports(int kind) const {
    return kind == InputAction::AXIS || kind == InputAction::VECTOR || kind == InputAction::MOTION;
}

Vector2 InputInvert::apply(const Vector2 &value) const {
    return {x ? -value.x : value.x, y ? -value.y : value.y};
}

void InputScale::_bind_methods() {
    ClassDB::bind_method(D_METHOD("set_factor", "value"), &InputScale::set_factor);
    ClassDB::bind_method(D_METHOD("get_factor"), &InputScale::get_factor);
    ADD_PROPERTY(PropertyInfo(Variant::VECTOR2, "factor"), "set_factor", "get_factor");
}

void InputScale::set_factor(const Vector2 &value) { factor = value; emit_changed(); }
Vector2 InputScale::get_factor() const { return factor; }

bool InputScale::supports(int kind) const {
    return (kind == InputAction::AXIS || kind == InputAction::VECTOR || kind == InputAction::MOTION) && factor.is_finite();
}

Vector2 InputScale::apply(const Vector2 &value) const {
    return value * factor;
}

void InputNormalize::_bind_methods() {}

bool InputNormalize::supports(int kind) const {
    return kind == InputAction::VECTOR;
}

Vector2 InputNormalize::apply(const Vector2 &value) const {
    const double length = precise_length(value);
    return length > 0.0 ?
            Vector2(static_cast<real_t>(value.x / length), static_cast<real_t>(value.y / length)) : Vector2();
}

void InputResponseCurve::_bind_methods() {
    ClassDB::bind_method(D_METHOD("set_exponent", "value"), &InputResponseCurve::set_exponent);
    ClassDB::bind_method(D_METHOD("get_exponent"), &InputResponseCurve::get_exponent);
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "exponent", PROPERTY_HINT_RANGE, "0.01,16,0.01"), "set_exponent", "get_exponent");
}

void InputResponseCurve::set_exponent(double value) { exponent = value; emit_changed(); }
double InputResponseCurve::get_exponent() const { return exponent; }

bool InputResponseCurve::supports(int kind) const {
    return (kind == InputAction::AXIS || kind == InputAction::VECTOR) && std::isfinite(exponent) && exponent >= 0.01 && exponent <= 16.0;
}

Vector2 InputResponseCurve::apply(const Vector2 &value) const {
    return {curve_component(value.x, exponent), curve_component(value.y, exponent)};
}

} // namespace crimson::input
