// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#include "register_input.hpp"

#include "controls.hpp"
#include "input_router.hpp"
#include "rebinding/register_rebinding.hpp"

namespace crimson::input {

void register_types(godot::ModuleInitializationLevel level) {
    if (level != godot::MODULE_INITIALIZATION_LEVEL_SCENE) {
        return;
    }
    GDREGISTER_ABSTRACT_CLASS(InputModifier);
    GDREGISTER_CLASS(InputDeadZone);
    GDREGISTER_CLASS(InputSwapAxis);
    GDREGISTER_CLASS(InputInvert);
    GDREGISTER_CLASS(InputScale);
    GDREGISTER_CLASS(InputNormalize);
    GDREGISTER_CLASS(InputResponseCurve);
    GDREGISTER_CLASS(InputBinding);
    GDREGISTER_CLASS(InputAction);
    GDREGISTER_CLASS(InputActionMapping);
    GDREGISTER_CLASS(InputActionMap);
    GDREGISTER_CLASS(InputLayout);
    GDREGISTER_CLASS(Controls);
    GDREGISTER_INTERNAL_CLASS(InputRouter);
    rebinding::register_types(level);
}

} // namespace crimson::input
