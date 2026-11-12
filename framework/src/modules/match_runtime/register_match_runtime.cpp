// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#include "register_match_runtime.hpp"

#include "command_router.hpp"
#include "player_bindings.hpp"
#include "possession.hpp"
#include "transaction_coordinator.hpp"

#include <godot_cpp/core/class_db.hpp>

namespace crimson::match_runtime {

void register_types(godot::ModuleInitializationLevel level) {
    if (level != godot::MODULE_INITIALIZATION_LEVEL_SCENE) {
        return;
    }
    GDREGISTER_CLASS(GameTransaction);
    GDREGISTER_CLASS(TransactionCoordinator);
    GDREGISTER_CLASS(PlayerBindings);
    GDREGISTER_CLASS(PlayerPossession);
    GDREGISTER_CLASS(CommandRouter);
}

} // namespace crimson::match_runtime
