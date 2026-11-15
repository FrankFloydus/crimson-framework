// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#include "modules/input/editor/register_input_editor.hpp"

#ifdef DEBUG_ENABLED

#include "editor/ui/editor_controls.hpp"
#include "modules/input/editor/input_actions_window.hpp"
#include "modules/input/editor/input_map_window.hpp"

#include <godot_cpp/classes/button.hpp>
#include <godot_cpp/classes/confirmation_dialog.hpp>
#include <godot_cpp/classes/editor_inspector_plugin.hpp>
#include <godot_cpp/classes/editor_plugin.hpp>
#include <godot_cpp/classes/editor_plugin_registration.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/callable_method_pointer.hpp>

namespace crimson::input {
namespace {

using namespace godot;

// The editor owns both the dedicated map editor and the compatibility window.
InputActionsWindow *legacy_window = nullptr;
InputMapWindow *map_window = nullptr;

// Keep resource-specific authoring available alongside the stock Inspector metadata.
class InputAuthoringInspector : public EditorInspectorPlugin {
    GDCLASS(InputAuthoringInspector, EditorInspectorPlugin)

    // Open a layout's maps or its legacy inline actions.
    void edit_layout(const Ref<InputLayout> &layout) { open_input_actions(layout); }
    // Open one independent map directly.
    void edit_map(const Ref<InputActionMap> &map) { open_input_map(map); }

protected:
    // The Inspector integration has no runtime bindings.
    static void _bind_methods() {}

public:
    // Match only the two asset types that lead to dedicated input authoring.
    bool _can_handle(Object *object) const override {
        return Object::cast_to<InputLayout>(object) || Object::cast_to<InputActionMap>(object);
    }

    // Add the editor entry point before the stock resource fields.
    void _parse_begin(Object *object) override {
        auto *button = memnew(Button);
        button->set_text("Edit Input Map");
        if (auto *map = Object::cast_to<InputActionMap>(object)) {
            button->set_tooltip_text("Open this map's actions, physical bindings and ordered modifiers.");
            button->connect("pressed", callable_mp(this, &InputAuthoringInspector::edit_map).bind(Ref<InputActionMap>(map)));
        } else {
            button->set_tooltip_text("Open a map referenced by this layout. Older layouts with inline actions use the compatibility editor.");
            button->connect("pressed", callable_mp(this, &InputAuthoringInspector::edit_layout)
                    .bind(Ref<InputLayout>(Object::cast_to<InputLayout>(object))));
        }
        add_custom_control(button);
    }

    // Let the map editor own mapping edits, while the Inspector still exposes layout configuration.
    bool _parse_property(Object *object, Variant::Type, const String &name, PropertyHint, const String &,
            BitField<PropertyUsageFlags>, bool) override {
        return Object::cast_to<InputActionMap>(object) ? name == "mappings" : name == "actions";
    }
};

// Own editor windows and all of their entry points.
class InputAuthoringPlugin : public EditorPlugin {
    GDCLASS(InputAuthoringPlugin, EditorPlugin)

    Ref<InputAuthoringInspector> inspector; ///< Resource entry points registered for this editor session.
    ConfirmationDialog *map_chooser = nullptr; ///< Selects one standalone map from a multi-map layout.
    crimson::editor_ui::Picker *map_options = nullptr; ///< Selects from valid entries in layout order.
    TypedArray<InputActionMap> pending_maps; ///< Maps snapshot while their display names are offered.
    Ref<InputLayout> pending_layout; ///< Layout retained while its map chooser is open.
    ObjectID pending_owner; ///< Optional scene or profile that opened the map chooser.

    // Reopen the last draft or start a new standalone map.
    void open_from_menu() { map_window->open({}, {}, nullptr, true); }

    // Open the selected map, then release the chooser context.
    void map_selected() {
        const int index = map_options->get_selected_index();
        if (pending_layout.is_valid() && index >= 0 && index < pending_maps.size()) {
            const Ref<InputActionMap> map = pending_maps[index];
            map_window->open(map, pending_layout, ObjectDB::get_instance(pending_owner));
        }
        map_selection_canceled();
    }

    // Release the chooser context after dismissal.
    void map_selection_canceled() {
        pending_layout.unref();
        pending_maps.clear();
        pending_owner = ObjectID();
    }

protected:
    // The editor owns this plugin.
    static void _bind_methods() {}

    // Install or remove input authoring with the editor session.
    void _notification(int what) {
        if (what == NOTIFICATION_ENTER_TREE) {
            legacy_window = memnew(InputActionsWindow);
            legacy_window->build();
            add_child(legacy_window);
            map_window = memnew(InputMapWindow);
            map_window->build();
            add_child(map_window);
            map_chooser = memnew(ConfirmationDialog);
            map_chooser->set_theme(map_window->get_theme());
            map_chooser->set_auto_translate_mode(Node::AUTO_TRANSLATE_MODE_DISABLED);
            map_chooser->set_title("Choose Input Map");
            map_chooser->set_ok_button_text("Open Map");
            add_child(map_chooser);
            map_options = crimson::editor_ui::select(map_chooser, {}, -1);
            map_options->set_name("LayoutMapOptions");
            map_options->get_trigger()->set_tooltip_text("Choose one of the standalone maps referenced by this layout.");
            map_chooser->connect("confirmed", callable_mp(this, &InputAuthoringPlugin::map_selected));
            map_chooser->connect("canceled", callable_mp(this, &InputAuthoringPlugin::map_selection_canceled));
            inspector.instantiate();
            add_inspector_plugin(inspector);
            add_tool_menu_item("Input Maps", callable_mp(this, &InputAuthoringPlugin::open_from_menu));
        } else if (what == NOTIFICATION_EXIT_TREE) {
            remove_tool_menu_item("Input Maps");
            if (inspector.is_valid()) {
                remove_inspector_plugin(inspector);
                inspector.unref();
            }
            legacy_window = nullptr;
            map_window = nullptr;
        }
    }

public:
    // Choose one resource before opening the dedicated editor.
    void choose_layout_map(const Ref<InputLayout> &layout, Object *owner) {
        pending_layout = layout;
        pending_owner = ObjectID(owner ? owner->get_instance_id() : 0);
        pending_maps.clear();
        PackedStringArray captions;
        for (const Variant &entry : layout->get_maps()) {
            const Ref<InputActionMap> map = entry;
            if (map.is_valid()) {
                const String title = map->get_display_name();
                captions.append(title.is_empty() ? String(map->get_id()) : title);
                pending_maps.append(map);
            }
        }
        if (captions.is_empty()) {
            map_selection_canceled();
            map_window->open({}, layout, owner);
            return;
        }
        map_options->set_items(captions, 0);
        map_chooser->popup_centered(Vector2i(crimson::editor_ui::pixels(map_chooser, 340), 0));
        map_options->get_trigger()->grab_focus();
    }
};

} // namespace

void open_input_actions(const godot::Ref<InputLayout> &layout, godot::Object *assignment_owner) {
    if (!map_window) {
        return;
    }
    if (layout.is_valid() && layout->get_maps().size() > 1) {
        if (auto *plugin = Object::cast_to<InputAuthoringPlugin>(map_window->get_parent())) {
            plugin->choose_layout_map(layout, assignment_owner);
        }
    } else if (layout.is_valid() && layout->get_maps().size() == 1) {
        const Ref<InputActionMap> map = layout->get_maps()[0];
        map_window->open(map, layout, assignment_owner);
    } else if (layout.is_valid() && !layout->get_actions().is_empty()) {
        legacy_window->open(layout, assignment_owner);
    } else {
        map_window->open({}, layout, assignment_owner);
    }
}

void open_input_map(const godot::Ref<InputActionMap> &map) {
    if (map_window) {
        map_window->open(map);
    }
}

void register_input_editor(godot::ModuleInitializationLevel level) {
    if (level == godot::MODULE_INITIALIZATION_LEVEL_EDITOR) {
        GDREGISTER_INTERNAL_CLASS(InputActionsWindow);
        GDREGISTER_INTERNAL_CLASS(InputMapWindow);
        GDREGISTER_INTERNAL_CLASS(InputAuthoringInspector);
        GDREGISTER_INTERNAL_CLASS(InputAuthoringPlugin);
        godot::EditorPlugins::add_by_type<InputAuthoringPlugin>();
    }
}

void unregister_input_editor(godot::ModuleInitializationLevel level) {
    if (level == godot::MODULE_INITIALIZATION_LEVEL_EDITOR) {
        godot::EditorPlugins::remove_by_type<InputAuthoringPlugin>();
    }
}

} // namespace crimson::input

#else

namespace crimson::input {

// Release templates have no editor surface or hooks.
void open_input_actions(const godot::Ref<InputLayout> &, godot::Object *) {}
void open_input_map(const godot::Ref<InputActionMap> &) {}
void register_input_editor(godot::ModuleInitializationLevel) {}
void unregister_input_editor(godot::ModuleInitializationLevel) {}

} // namespace crimson::input

#endif
