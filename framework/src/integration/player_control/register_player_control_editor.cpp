// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#include "integration/player_control/register_player_control_editor.hpp"

#ifdef DEBUG_ENABLED

#include "integration/player_control/controls_component.hpp"
#include "modules/mover/register_mover_editor.hpp"

#include <godot_cpp/classes/button.hpp>
#include <godot_cpp/classes/editor_inspector.hpp>
#include <godot_cpp/classes/editor_inspector_plugin.hpp>
#include <godot_cpp/classes/editor_plugin.hpp>
#include <godot_cpp/classes/editor_plugin_registration.hpp>
#include <godot_cpp/classes/editor_property.hpp>
#include <godot_cpp/classes/option_button.hpp>
#include <godot_cpp/classes/popup_menu.hpp>
#include <godot_cpp/classes/style_box_empty.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/callable_method_pointer.hpp>

#include <utility>

namespace crimson::integration {
namespace {

using namespace godot;
using input::InputAction;
using input::InputLayout;

// Edit one persisted StringName by choosing a compatible action from the current Input Layout.
class ProfileActionProperty : public EditorProperty {
    GDCLASS(ProfileActionProperty, EditorProperty)

    OptionButton *choices = nullptr; ///< Inspector dropdown for the edited field.
    InputAction::ValueKind required_kind = InputAction::VECTOR; ///< Action value accepted by this field.
    String disabled_help; ///< Effect of choosing None, or empty for a required action.
    bool read_only = false; ///< Mirrors the current Inspector editability.

    // Add a choice and its help without changing the underlying stored ID.
    void add_choice(const StringName &id, const String &text, const String &help, bool enabled = true) {
        const int index = choices->get_item_count();
        choices->add_item(text);
        choices->set_item_metadata(index, id);
        choices->set_item_tooltip(index, help);
        choices->set_item_disabled(index, !enabled);
    }

    // Forward the selected ID to the normal Inspector change and undo path.
    void on_selected(int64_t index) {
        if (!read_only && index >= 0 && index < choices->get_item_count() && !choices->is_item_disabled(index)) {
            emit_changed(get_edited_property(), choices->get_item_metadata(index));
        }
    }

    // Rebuild from nested resources edited elsewhere and put keyboard focus on an available item.
    void on_popup() {
        _update_property();
        PopupMenu *popup = choices->get_popup();
        const int selected = choices->get_selected();
        if (selected >= 0 && !choices->is_item_disabled(selected)) {
            popup->set_focused_item(selected);
            return;
        }
        for (int i = 0; i < choices->get_item_count(); ++i) {
            if (!choices->is_item_disabled(i)) {
                popup->set_focused_item(i);
                return;
            }
        }
    }

protected:
    // This editor control has no game-facing methods.
    static void _bind_methods() {}

public:
    // Create a native editor property with keyboard and mouse navigation.
    void configure(InputAction::ValueKind kind, const String &help, const String &none_help = {}) {
        required_kind = kind;
        disabled_help = none_help;
        choices = memnew(OptionButton);
        Ref<StyleBoxEmpty> focus;
        focus.instantiate();
        choices->add_theme_stylebox_override("focus", focus);
        choices->set_fit_to_longest_item(false);
        choices->set_tooltip_text(help);
        choices->connect("item_selected", callable_mp(this, &ProfileActionProperty::on_selected));
        choices->get_popup()->connect("about_to_popup", callable_mp(this, &ProfileActionProperty::on_popup));
        add_child(choices);
        add_focusable(choices);
    }

    // Refresh choices from the current layout and preserve any unmatched authored ID.
    void _update_property() override {
        Object *object = get_edited_object();
        if (!object) {
            return;
        }
        const StringName current = object->get(get_edited_property());
        choices->clear();
        int selected = -1;
        if (!disabled_help.is_empty()) {
            add_choice(StringName(), "None", disabled_help);
            if (current.is_empty()) {
                selected = 0;
            }
        }

        const Ref<InputLayout> layout = object->get("input_layout");
        if (layout.is_valid()) {
            const TypedArray<InputAction> actions = layout->resolved_actions();
            for (int i = 0; i < actions.size(); ++i) {
                const Ref<InputAction> action = actions[i];
                if (action.is_null() || action->get_kind() != required_kind || action->get_id().is_empty()) {
                    continue;
                }
                const StringName id = action->get_id();
                if (id == current) {
                    selected = choices->get_item_count();
                }
                const String name = action->get_display_name();
                const String label = name.is_empty() || name == String(id) ? String(id) :
                        name + String(" (") + String(id) + String(")");
                add_choice(id, label, String("Use the bindings from the ") + label + " action in this Input Layout.");
            }
        }

        if (selected < 0) {
            selected = choices->get_item_count();
            const String placeholder = layout.is_null() ? "Assign an input layout" : "Choose an action";
            const String label = current.is_empty() ? placeholder : String(current) + String(" (unavailable)");
            add_choice(current, label, layout.is_null() ? "Assign an Input Layout to see its actions." :
                    "Choose a compatible action or update the action's kind in the Input Layout.", false);
        }
        choices->select(selected);
    }

    // Keep the dropdown read-only whenever the stock Inspector forbids editing.
    void _set_read_only(bool value) override {
        read_only = value;
        choices->set_disabled(value);
    }
};

// Attach choice help to Controller Style and provide typed action pickers.
class PlayerControlProfileInspector : public EditorInspectorPlugin {
    GDCLASS(PlayerControlProfileInspector, EditorInspectorPlugin)

    bool creating_stock_editor = false; ///< Prevent recursion when Godot resolves the normal enum editor.

protected:
    // These tools are private to the Godot editor.
    static void _bind_methods() {}

public:
    // Add authoring help only to the profile and controls component.
    bool _can_handle(Object *object) const override {
        return !creating_stock_editor && (Object::cast_to<PlayerControlProfile>(object) || Object::cast_to<ControlsComponent>(object));
    }

    // Keep the stock style editor and replace action names with typed selectors.
    bool _parse_property(Object *object, Variant::Type type, const String &name, PropertyHint hint,
            const String &hint_text, BitField<PropertyUsageFlags> usage, bool wide) override {
        if (name == "controller_style" && Object::cast_to<PlayerControlProfile>(object)) {
            const bool previous = std::exchange(creating_stock_editor, true);
            EditorProperty *row = EditorInspector::instantiate_property_editor(object, type, name, hint, hint_text, usage, wide);
            creating_stock_editor = previous;
            if (!row) {
                return false;
            }
            mover::apply_controller_style_help(*row);
            add_property_editor(name, row);
            return true;
        }

        const bool move = name == "move_action";
        const bool jump = name == "jump_action";
        const bool mouse = name == "mouse_look_action";
        if (!move && !jump && !mouse && name != "stick_look_action") {
            return false;
        }
        ProfileActionProperty *picker = memnew(ProfileActionProperty);
        if (jump) {
            picker->configure(InputAction::BUTTON, "Choose the button action to press for a grounded jump.",
                    "Disable jump input from these controls.");
        } else if (move) {
            picker->configure(InputAction::VECTOR, "Choose the directional action that drives movement.");
        } else if (mouse) {
            picker->configure(InputAction::MOTION, "Choose the relative mouse motion action for aiming.",
                    "Disable mouse look from these controls.");
        } else {
            picker->configure(InputAction::VECTOR, "Choose the directional stick action for aiming.",
                    "Disable stick look from these controls.");
        }
        add_property_editor(name, picker);
        return true;
    }
};

// Install the authoring help with the native editor plugin lifecycle.
class PlayerControlAuthoringPlugin : public EditorPlugin {
    GDCLASS(PlayerControlAuthoringPlugin, EditorPlugin)

    Ref<PlayerControlProfileInspector> inspector; ///< Local controls inspector registered with Godot.

protected:
    // The extension exposes no gameplay setup node for this plugin.
    static void _bind_methods() {}

    // Add and remove the inspector together with its editor owner.
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

void register_player_control_editor(godot::ModuleInitializationLevel level) {
    if (level == godot::MODULE_INITIALIZATION_LEVEL_EDITOR) {
        GDREGISTER_INTERNAL_CLASS(ProfileActionProperty);
        GDREGISTER_INTERNAL_CLASS(PlayerControlProfileInspector);
        GDREGISTER_INTERNAL_CLASS(PlayerControlAuthoringPlugin);
        godot::EditorPlugins::add_by_type<PlayerControlAuthoringPlugin>();
    }
}

void unregister_player_control_editor(godot::ModuleInitializationLevel level) {
    if (level == godot::MODULE_INITIALIZATION_LEVEL_EDITOR) {
        godot::EditorPlugins::remove_by_type<PlayerControlAuthoringPlugin>();
    }
}

} // namespace crimson::integration

#else

namespace crimson::integration {

// Release templates do not load the profile Inspector.
void register_player_control_editor(godot::ModuleInitializationLevel) {}

// Release templates have no profile Inspector to remove.
void unregister_player_control_editor(godot::ModuleInitializationLevel) {}

} // namespace crimson::integration

#endif
