// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#include "register_rebinding.hpp"

#include "rebinding.hpp"

#include <godot_cpp/core/class_db.hpp>

namespace crimson::rebinding {

void register_types(godot::ModuleInitializationLevel level) {
    if (level == godot::MODULE_INITIALIZATION_LEVEL_SCENE) {
        GDREGISTER_CLASS(BindingSlot);
        GDREGISTER_CLASS(RebindResult);
        GDREGISTER_INTERNAL_CLASS(RebindOperation);
        GDREGISTER_CLASS(Rebinding);
    }
}

} // namespace crimson::rebinding
