// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#pragma once

#include <godot_cpp/classes/resource.hpp>
#include <godot_cpp/variant/vector2.hpp>

namespace crimson::input {

// A stateless transformation of one binding's value before action aggregation.
class InputModifier : public godot::Resource {
    GDCLASS(InputModifier, godot::Resource)

protected:
    // Register the base type for ordered modifier resources.
    static void _bind_methods();

public:
    // Check that this transform is valid for the given InputAction::ValueKind.
    virtual bool supports(int kind) const = 0;
    // Transform one source value. The source and action determine its units.
    virtual godot::Vector2 apply(const godot::Vector2 &value) const = 0;
};

// Remap a source through lower and upper thresholds in normalized units.
class InputDeadZone : public InputModifier {
    GDCLASS(InputDeadZone, InputModifier)

public:
    // Apply the thresholds to vector length or to each component separately.
    enum Mode { RADIAL, AXIAL };

private:
    // How to measure the source before thresholding.
    Mode mode = RADIAL;
    // Minimum absolute value or length needed to produce a level.
    double lower_threshold = 0.15;
    // Absolute value or length that already reaches full output.
    double upper_threshold = 1.0;

protected:
    // Bind the threshold and mode authoring properties.
    static void _bind_methods();

public:
    // Set radial or axial thresholding.
    void set_mode(Mode value);
    // Return the threshold measurement mode.
    Mode get_mode() const;
    // Set the normalized threshold for leaving zero.
    void set_lower_threshold(double value);
    // Return the normalized threshold for leaving zero.
    double get_lower_threshold() const;
    // Set the normalized threshold for reaching full output.
    void set_upper_threshold(double value);
    // Return the normalized threshold for reaching full output.
    double get_upper_threshold() const;
    // Accept scalar and vector levels with valid ordered thresholds.
    bool supports(int kind) const override;
    // Preserve signs and remap the configured measure through both thresholds.
    godot::Vector2 apply(const godot::Vector2 &value) const override;
};

// Route two-dimensional source components in their original or swapped order.
class InputSwapAxis : public InputModifier {
    GDCLASS(InputSwapAxis, InputModifier)

public:
    // Select the source components for output X and Y in order.
    enum Order { XY, YX };

private:
    // Output component order. YX swaps the two source axes.
    Order order = YX;

protected:
    // Bind the output component order.
    static void _bind_methods();

public:
    // Set which input components become output X and Y.
    void set_order(Order value);
    // Return the configured output component order.
    Order get_order() const;
    // Accept vector levels and relative mouse motion.
    bool supports(int kind) const override;
    // Route the two components without rescaling them.
    godot::Vector2 apply(const godot::Vector2 &value) const override;
};

// Invert selected components of one binding.
class InputInvert : public InputModifier {
    GDCLASS(InputInvert, InputModifier)

    // Whether the source X component changes sign.
    bool x = true;
    // Whether the source Y component changes sign.
    bool y = false;

protected:
    // Bind the component toggles.
    static void _bind_methods();

public:
    // Select whether to invert X.
    void set_x(bool value);
    // Report whether X changes sign.
    bool get_x() const;
    // Select whether to invert Y.
    void set_y(bool value);
    // Report whether Y changes sign.
    bool get_y() const;
    // Accept scalar, vector, and motion bindings.
    bool supports(int kind) const override;
    // Flip signs without changing magnitudes.
    godot::Vector2 apply(const godot::Vector2 &value) const override;
};

// Multiply one binding by independent component gains.
class InputScale : public InputModifier {
    GDCLASS(InputScale, InputModifier)

    // X and Y multipliers applied before action aggregation.
    godot::Vector2 factor{1, 1};

protected:
    // Bind the scale factor.
    static void _bind_methods();

public:
    // Set the multipliers for both components.
    void set_factor(const godot::Vector2 &value);
    // Return the authored component gains.
    godot::Vector2 get_factor() const;
    // Accept scalar, vector, and motion bindings with finite gains.
    bool supports(int kind) const override;
    // Multiply each component by its gain.
    godot::Vector2 apply(const godot::Vector2 &value) const override;
};

// Give every nonzero two-dimensional level unit magnitude.
class InputNormalize : public InputModifier {
    GDCLASS(InputNormalize, InputModifier)

protected:
    // Register this parameterless transform.
    static void _bind_methods();

public:
    // Accept normalized two-dimensional levels.
    bool supports(int kind) const override;
    // Preserve direction and normalize every nonzero source.
    godot::Vector2 apply(const godot::Vector2 &value) const override;
};

// Shape analog levels by applying a sign-preserving exponent per component.
class InputResponseCurve : public InputModifier {
    GDCLASS(InputResponseCurve, InputModifier)

    // Exponent applied to each absolute component in the normalized range.
    double exponent = 1.0;

protected:
    // Bind the response exponent.
    static void _bind_methods();

public:
    // Set the normalized response exponent.
    void set_exponent(double value);
    // Return the authored response exponent.
    double get_exponent() const;
    // Accept scalar and vector levels with an exponent in the supported range.
    bool supports(int kind) const override;
    // Shape component magnitudes and preserve their signs.
    godot::Vector2 apply(const godot::Vector2 &value) const override;
};

} // namespace crimson::input

VARIANT_ENUM_CAST(crimson::input::InputDeadZone::Mode);
VARIANT_ENUM_CAST(crimson::input::InputSwapAxis::Order);
