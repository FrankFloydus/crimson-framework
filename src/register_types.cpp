// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#include "integration/crimson_game.hpp"
#include "integration/inventory_transaction_adapter.hpp"
#include "modules/replication/register_replication.hpp"
#include "modules/inventory/register_inventory.hpp"
#include "modules/match_runtime/register_match_runtime.hpp"
#include "modules/save/register_save.hpp"
#include "modules/scene_load/register_scene_load.hpp"
#include "modules/session/register_session.hpp"

#include <gdextension_interface.h>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/core/defs.hpp>
#include <godot_cpp/godot.hpp>

namespace {

// Register framework modules and integration adapters in dependency order.
void initialize_crimson_framework(godot::ModuleInitializationLevel level) {
    crimson::inventory::register_types(level);
    crimson::save::initialize(level);
    crimson::session::register_types(level);
    crimson::scene_load::register_types(level);
    crimson::match_runtime::register_types(level);
    crimson::replication::register_types(level);
    if (level == godot::MODULE_INITIALIZATION_LEVEL_SCENE) {
        GDREGISTER_CLASS(crimson::integration::CrimsonGameDefinition);
        GDREGISTER_CLASS(crimson::integration::CrimsonGameOperation);
        GDREGISTER_INTERNAL_CLASS(crimson::integration::CrimsonMatchCheckpointCandidate);
        GDREGISTER_INTERNAL_CLASS(crimson::integration::CrimsonMatchCheckpoint);
        GDREGISTER_INTERNAL_CLASS(crimson::integration::CrimsonMatchNetwork);
        GDREGISTER_CLASS(crimson::integration::CrimsonSaveRequest);
        GDREGISTER_CLASS(crimson::integration::CrimsonSaveSlot);
        GDREGISTER_CLASS(crimson::integration::CrimsonGame);
        GDREGISTER_CLASS(crimson::integration::CrimsonInventoryTransactionAdapter);
    }
}

// Release module resources when Godot shuts down the extension.
void uninitialize_crimson_framework(godot::ModuleInitializationLevel level) {
    crimson::replication::unregister_types(level);
    crimson::scene_load::unregister_types(level);
    crimson::session::unregister_types(level);
    crimson::save::uninitialize(level);
    crimson::inventory::unregister_types(level);
}

} // namespace

// Bind Crimson Framework to the Godot GDExtension lifecycle.
extern "C" GDExtensionBool GDE_EXPORT crimson_framework_init(
        GDExtensionInterfaceGetProcAddress get_proc_address,
        GDExtensionClassLibraryPtr library,
        GDExtensionInitialization *initialization) {
    godot::GDExtensionBinding::InitObject init(get_proc_address, library, initialization);
    init.register_initializer(initialize_crimson_framework);
    init.register_terminator(uninitialize_crimson_framework);
    init.set_minimum_library_initialization_level(godot::MODULE_INITIALIZATION_LEVEL_SCENE);
    return init.init();
}
