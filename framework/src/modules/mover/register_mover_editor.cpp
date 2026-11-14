// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#include "register_mover_editor.hpp"

#ifdef DEBUG_ENABLED

#include "mover_component.hpp"

#include <godot_cpp/classes/editor_inspector.hpp>
#include <godot_cpp/classes/editor_inspector_plugin.hpp>
#include <godot_cpp/classes/editor_plugin.hpp>
#include <godot_cpp/classes/editor_plugin_registration.hpp>
#include <godot_cpp/classes/editor_property.hpp>
#include <godot_cpp/classes/option_button.hpp>
#include <godot_cpp/core/class_db.hpp>

#include <cstdint>
#include <utility>

namespace crimson::mover {

// Look up the description from the editor's copy of the owning class XML.
godot::String get_mover_property_help(const godot::StringName &property);
// Look up a Controller Style choice by its stored value in the owning class XML.
godot::String get_mover_controller_style_help(int64_t value);

namespace {

using namespace godot;

// Use the same integer metadata Godot's enum editor submits when a choice is selected.
void add_controller_style_help(OptionButton &options) {
    for (int index = 0; index < options.get_item_count(); ++index) {
        const Variant value = options.get_item_metadata(index);
        if (value.get_type() == Variant::INT && options.get_item_tooltip(index).is_empty()) {
            options.set_item_tooltip(index, get_mover_controller_style_help(value));
        }
    }
}

// Stock value controls stop tooltip propagation from the property row.
void apply_property_help(EditorProperty &row, const String &help, bool controller_style) {
    for (int index = 0; index < row.get_child_count(); ++index) {
        auto *control = Object::cast_to<Control>(row.get_child(index));
        if (control && control->get_mouse_filter() == Control::MOUSE_FILTER_STOP && control->get_tooltip_text().is_empty()) {
            control->set_tooltip_text(help);
        }
        if (controller_style) {
            if (auto *options = Object::cast_to<OptionButton>(control)) {
                add_controller_style_help(*options);
            }
        }
    }
}

// Keep Godot's property editor and let its value widget show the same authored help.
class MoverPropertyHelpInspector : public EditorInspectorPlugin {
    GDCLASS(MoverPropertyHelpInspector, EditorInspectorPlugin)

    bool creating_stock_editor = false; ///< Prevent this plugin from wrapping itself when Godot resolves the editor.

protected:
    // This editor plugin exposes no script API.
    static void _bind_methods() {}

public:
    // Only MoverComponent value editors need this help attachment.
    bool _can_handle(Object *object) const override {
        return !creating_stock_editor && Object::cast_to<MoverComponent>(object);
    }

    // Ask Godot for the standard editor so all native editing and undo behavior stays in use.
    bool _parse_property(Object *object, Variant::Type type, const String &name, PropertyHint hint,
            const String &hint_text, BitField<PropertyUsageFlags> usage, bool wide) override {
        const String help = get_mover_property_help(name);
        if (help.is_empty()) {
            return false;
        }
        const bool previous = std::exchange(creating_stock_editor, true);
        EditorProperty *row = EditorInspector::instantiate_property_editor(object, type, name, hint, hint_text, usage, wide);
        creating_stock_editor = previous;
        if (!row) {
            return false;
        }
        apply_property_help(*row, help, name == "controller_style");
        add_property_editor(name, row);
        return true;
    }
};

// Attach the private Inspector plugin for the lifetime of the Godot editor.
class MoverAuthoringPlugin : public EditorPlugin {
    GDCLASS(MoverAuthoringPlugin, EditorPlugin)

    Ref<MoverPropertyHelpInspector> inspector; ///< Inspector retained while registered with the editor.

protected:
    // This editor plugin exposes no script API.
    static void _bind_methods() {}

    // Register and remove the help integration together with its editor owner.
    void _notification(int what) {
        if (what == NOTIFICATION_ENTER_TREE) {
            inspector.instantiate();
            add_inspector_plugin(inspector);
        } else if (what == NOTIFICATION_EXIT_TREE && inspector.is_valid()) {
            remove_inspector_plugin(inspector);
            inspector.unref();
        }
    }
};

} // namespace

void apply_controller_style_help(godot::EditorProperty &row) {
    apply_property_help(row, get_mover_property_help("controller_style"), true);
}

void register_mover_editor(godot::ModuleInitializationLevel level) {
    if (level == godot::MODULE_INITIALIZATION_LEVEL_EDITOR) {
        GDREGISTER_INTERNAL_CLASS(MoverPropertyHelpInspector);
        GDREGISTER_INTERNAL_CLASS(MoverAuthoringPlugin);
        godot::EditorPlugins::add_by_type<MoverAuthoringPlugin>();
    }
}

void unregister_mover_editor(godot::ModuleInitializationLevel level) {
    if (level == godot::MODULE_INITIALIZATION_LEVEL_EDITOR) {
        godot::EditorPlugins::remove_by_type<MoverAuthoringPlugin>();
    }
}

} // namespace crimson::mover

#else

namespace crimson::mover {

// Release templates do not load the Mover Inspector.
void register_mover_editor(godot::ModuleInitializationLevel) {}

// Release templates have no Mover Inspector to remove.
void unregister_mover_editor(godot::ModuleInitializationLevel) {}

} // namespace crimson::mover

#endif
