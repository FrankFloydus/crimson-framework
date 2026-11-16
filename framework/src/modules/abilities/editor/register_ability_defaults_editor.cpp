// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#include "modules/abilities/editor/register_ability_defaults_editor.hpp"

#ifdef DEBUG_ENABLED

#include "editor/ui/editor_compositions.hpp"
#include "editor/ui/editor_controls.hpp"
#include "editor/ui/editor_spacing.hpp"
#include "modules/abilities/ability_defaults.hpp"
#include "modules/gameplay_tags/editor/gameplay_tag_picker.hpp"

#include <godot_cpp/classes/button.hpp>
#include <godot_cpp/classes/container.hpp>
#include <godot_cpp/classes/editor_inspector_plugin.hpp>
#include <godot_cpp/classes/editor_interface.hpp>
#include <godot_cpp/classes/editor_plugin.hpp>
#include <godot_cpp/classes/editor_plugin_registration.hpp>
#include <godot_cpp/classes/h_box_container.hpp>
#include <godot_cpp/classes/input_event.hpp>
#include <godot_cpp/classes/label.hpp>
#include <godot_cpp/classes/margin_container.hpp>
#include <godot_cpp/classes/panel_container.hpp>
#include <godot_cpp/classes/scroll_container.hpp>
#include <godot_cpp/classes/style_box_flat.hpp>
#include <godot_cpp/classes/v_box_container.hpp>
#include <godot_cpp/classes/window.hpp>
#include <godot_cpp/variant/callable_method_pointer.hpp>

#include <algorithm>
#include <array>

namespace crimson::abilities {
namespace {

using namespace godot;
namespace ui = crimson::editor_ui;

/// Authoring labels for the per-script tag defaults.
struct TagField {
    const char *key; ///< Registry property name.
    const char *label; ///< Label shown next to the picker field.
    const char *section; ///< Non-empty on the first field of each section.
};

constexpr TagField fields[] = {
    {"ability_tags", "Ability Tags", "Ability"},
    {"activation_required_tags", "Required Tags", "Activation"},
    {"activation_blocked_tags", "Blocked Tags", ""},
    {"activation_owned_tags", "Owned Tags", ""},
    {"block_abilities_with_tags", "Block Abilities With Tags", "Other Abilities"},
    {"cancel_abilities_with_tags", "Cancel Abilities With Tags", ""},
    {"source_required_tags", "Required Tags", "Source"},
    {"source_blocked_tags", "Blocked Tags", ""},
    {"target_required_tags", "Required Tags", "Target"},
    {"target_blocked_tags", "Blocked Tags", ""},
};

/// Return the selected script or the script attached to the selected node.
Ref<Script> selected_script(Object *object) {
    if (auto *script = Object::cast_to<Script>(object)) {
        return Ref<Script>(script);
    }
    const auto *node = Object::cast_to<Node>(object);
    return node ? Ref<Script>(node->get_script()) : Ref<Script>();
}

/// Keep a detached draft while the modal is open.
class AbilityDefaultsWindow final : public Window {
    GDCLASS(AbilityDefaultsWindow, Window)

    /// Per-field view owned by this window.
    struct Row {
        Button *trigger = nullptr; ///< Clickable field background and keyboard action.
        Container *chips = nullptr; ///< Wrapping display of the selected tags.
    };

    std::array<Row, std::size(fields)> rows; ///< Control pairs in field order.
    Ref<Script> script; ///< The saved script being edited.
    String opened_uid; ///< The persistent identity captured before any changes.
    Dictionary saved; ///< Snapshot read when the dialog opened.
    Dictionary draft; ///< Uncommitted selections.
    ObjectID anchor; ///< Focus target owned by the calling Inspector or graph.
    Label *file = nullptr; ///< The script file being edited.
    Label *status = nullptr; ///< Inline feedback for a load/save failure.
    Button *cancel_button = nullptr; ///< Close the draft without saving.
    Button *save_button = nullptr; ///< Persist valid changes.
    bool loaded = false; ///< Whether editing is available for this script.

    /// Read the selected tag paths for one field.
    PackedStringArray selected(int index) const {
        const Variant value = draft.get(fields[index].key, Variant());
        return value.get_type() == Variant::PACKED_STRING_ARRAY ? PackedStringArray(value) : PackedStringArray();
    }

    /// Keep errors in the form the author can recover from.
    void show_status(const String &message) {
        status->set_text(message);
        status->set_visible(!message.is_empty());
    }

    /// Rebuild one tag field while preserving its stable picker action.
    void refresh_row(int index) {
        const auto &row = rows[index];
        const PackedStringArray tags = selected(index);
        while (row.chips->get_child_count() > 0) {
            Node *old = row.chips->get_child(0);
            row.chips->remove_child(old);
            old->queue_free();
        }
        for (const String &path : tags) {
            auto *chip = ui::surface_panel(row.chips, ui::Surface::SIDEBAR);
            chip->set_mouse_filter(Control::MOUSE_FILTER_IGNORE);
            chip->add_theme_stylebox_override("panel", ui::box(ui::color(ui::Surface::SIDEBAR),
                    ui::color(ui::Surface::OUTLINE), 0, 0, get_theme_default_base_scale()));
            auto *contents = memnew(HBoxContainer);
            chip->add_child(contents);
            contents->set_mouse_filter(Control::MOUSE_FILTER_IGNORE);
            contents->add_theme_constant_override("separation", 0);
            auto *caption = ui::label(contents, path);
            const real_t needed = caption->get_combined_minimum_size().x;
            caption->set_text_overrun_behavior(TextServer::OVERRUN_TRIM_ELLIPSIS);
            caption->set_custom_minimum_size(Vector2(std::min<real_t>(needed + ui::pixels(this, 8), ui::pixels(this, 228)), 0));
            auto *remove = ui::icon_button(contents, "close", "Remove " + path + ".",
                    callable_mp(this, &AbilityDefaultsWindow::remove_tag).bind(index, path), ui::ButtonStyle::QUIET);
            remove->set_name("RemoveAbilityDefaultTag");
            remove->set_custom_minimum_size(Vector2(ui::pixels(this, 22), ui::pixels(this, 23)));
            remove->set_disabled(!loaded);
        }
        row.chips->set_visible(!tags.is_empty());
        row.trigger->set_text(tags.is_empty() ? "Choose Tags" : String());
        row.trigger->set_disabled(!loaded);
    }

    /// Keep an explicit edit in this window until the user saves the defaults.
    void set_tags(int index, const PackedStringArray &tags) {
        if (tags.is_empty()) {
            draft.erase(fields[index].key);
        } else {
            draft[fields[index].key] = tags.duplicate();
        }
        refresh_row(index);
        save_button->set_disabled(draft == saved);
        show_status({});
    }

    /// Remove one exact tag and move focus to the stable field surface.
    void remove_tag(int index, const String &path) {
        auto tags = selected(index).duplicate();
        if (loaded && tags.erase(path)) {
            set_tags(index, tags);
            rows[index].trigger->call_deferred("grab_focus");
        }
    }

    /// Reject a pending picker result if its row or draft no longer matches.
    bool accept_tags(const PackedStringArray &tags, int index, const PackedStringArray &previous) {
        if (!is_visible() || !loaded || selected(index) != previous) {
            return false;
        }
        if (tags != previous) {
            set_tags(index, tags);
        }
        return true;
    }

    /// Open the shared catalog picker with the current explicit choices.
    void choose_tags(int index) {
        auto *picker = gameplay_tags::find_gameplay_tag_picker();
        if (!loaded || !picker) {
            return;
        }
        const PackedStringArray previous = selected(index);
        const ObjectID focus(rows[index].trigger->get_instance_id());
        picker->open(previous, true, {}, ObjectID(get_instance_id()), focus, focus,
                callable_mp(this, &AbilityDefaultsWindow::accept_tags).bind(index, previous));
    }

    /// Publish an entire validated draft after checking for external edits.
    void save() {
        if (!loaded || draft == saved) {
            return;
        }
        if (get_ability_defaults_authoring_uid(script) != opened_uid) {
            show_status("The ability script changed. Close and reopen Ability Defaults to edit the current script.");
            return;
        }
        Dictionary current;
        String error;
        if (!load_ability_defaults(script, current, error)) {
            show_status(error);
            return;
        }
        if (current != saved) {
            show_status("The saved defaults changed. Close and reopen Ability Defaults to load the current values.");
            return;
        }
        if (save_ability_defaults(script, draft, opened_uid, error) != OK) {
            show_status(error);
            return;
        }
        close();
    }

    /// Retire the draft and restore its caller's focus if it still exists.
    void close() {
        if (auto *picker = gameplay_tags::find_gameplay_tag_picker()) {
            picker->dismiss_source(ObjectID(get_instance_id()));
        }
        set_process_input(false);
        hide();
        script.unref();
        opened_uid = String();
        if (auto *control = Object::cast_to<Control>(ObjectDB::get_instance(anchor))) {
            if (control->is_visible_in_tree()) {
                control->call_deferred("grab_focus");
            }
        }
        anchor = ObjectID();
    }

    /// Create a clickable tag surface with the same wrapping chips as the Inspector.
    void build_row(VBoxContainer *form, int index) {
        const auto &definition = fields[index];
        if (*definition.section) {
            ui::label(form, definition.section, ui::LabelStyle::HEADING);
        }
        auto *surface = memnew(PanelContainer);
        ui::field(form, definition.label, surface, {}, 150);
        surface->set_mouse_filter(Control::MOUSE_FILTER_IGNORE);
        surface->add_theme_stylebox_override("panel", ui::box(ui::color(ui::Surface::HEADER),
                ui::color(ui::Surface::OUTLINE), 0, 0, get_theme_default_base_scale()));
        auto &row = rows[index];
        row.trigger = ui::button(surface, "Choose Tags", "Choose " + String(definition.label).to_lower() + ".",
                callable_mp(this, &AbilityDefaultsWindow::choose_tags).bind(index), ui::ButtonStyle::QUIET);
        row.trigger->set_text_alignment(HORIZONTAL_ALIGNMENT_LEFT);
        row.trigger->set_name("AbilityDefaultsTags_" + String(definition.key));
        row.trigger->set_accessibility_name(String(definition.label));
        auto *inset = ui::inset(surface, ui::spacing::xs, ui::spacing::xs);
        inset->set_mouse_filter(Control::MOUSE_FILTER_IGNORE);
        const Variant flow = ClassDB::instantiate("HFlowContainer");
        row.chips = Object::cast_to<Container>(flow.get_validated_object());
        ERR_FAIL_NULL(row.chips);
        inset->add_child(row.chips);
        row.chips->set_name("AbilityDefaultsChips_" + String(definition.key));
        row.chips->set_mouse_filter(Control::MOUSE_FILTER_IGNORE);
        row.chips->add_theme_constant_override("h_separation", ui::pixels(this, ui::spacing::xs));
        row.chips->add_theme_constant_override("v_separation", ui::pixels(this, ui::spacing::xs));
        row.chips->hide();
    }

protected:
    /// This window exposes no game-facing methods.
    static void _bind_methods() {}

public:
    /// Build the shared window once per editor session.
    void configure() {
        hide();
        set_name("AbilityDefaultsWindow");
        set_title("Ability Defaults");
        set_accessibility_name("Ability Defaults");
        set_transient(true);
        set_transient_to_focused(true);
        set_exclusive(true);
        set_theme(ui::create_theme(this, EditorInterface::get_singleton()->get_editor_scale()));
        ui::embed_editor_popups(this);
        set_min_size(Vector2i(ui::pixels(this, 470), ui::pixels(this, 330)));
        auto *canvas = ui::surface_panel(this, ui::Surface::CANVAS);
        canvas->set_anchors_and_offsets_preset(Control::PRESET_FULL_RECT);
        auto *frame = ui::field_group(ui::inset(canvas, ui::spacing::lg, ui::spacing::lg));
        file = ui::label(frame, {}, ui::LabelStyle::MUTED);
        file->set_name("AbilityDefaultsScriptPath");
        file->set_text_overrun_behavior(TextServer::OVERRUN_TRIM_ELLIPSIS);
        auto *scroll = memnew(ScrollContainer);
        frame->add_child(scroll);
        scroll->set_v_size_flags(Control::SIZE_EXPAND_FILL);
        scroll->set_horizontal_scroll_mode(ScrollContainer::SCROLL_MODE_DISABLED);
        scroll->set_follow_focus(true);
        auto *form = ui::field_group(scroll);
        for (int index = 0; index < static_cast<int>(rows.size()); ++index) {
            build_row(form, index);
        }
        auto *footer = memnew(HBoxContainer);
        frame->add_child(footer);
        footer->set_alignment(BoxContainer::ALIGNMENT_END);
        status = ui::label(footer, {}, ui::LabelStyle::MUTED);
        status->set_name("AbilityDefaultsStatus");
        status->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
        status->set_h_size_flags(Control::SIZE_EXPAND_FILL);
        status->add_theme_color_override("font_color", ui::color(ui::Surface::DANGER));
        status->set_custom_minimum_size(Vector2(ui::pixels(this, 180), 0));
        cancel_button = ui::button(footer, "Cancel", "Discard changes and close Ability Defaults.",
                callable_mp(this, &AbilityDefaultsWindow::close), ui::ButtonStyle::QUIET);
        cancel_button->set_name("AbilityDefaultsCancel");
        save_button = ui::button(footer, "Save", "Save the defaults for this script.",
                callable_mp(this, &AbilityDefaultsWindow::save), ui::ButtonStyle::PRIMARY);
        save_button->set_name("AbilityDefaultsSave");
        connect("close_requested", callable_mp(this, &AbilityDefaultsWindow::close));
        set_process_input(false);
    }

    /// Load a fresh draft and keep an unsaved open dialog attached to its current script.
    void open(const Ref<Script> &selected, const String &uid, Control *return_focus) {
        if (is_visible()) {
            return;
        }
        anchor = ObjectID(return_focus ? return_focus->get_instance_id() : 0);
        script = selected;
        opened_uid = uid;
        saved = Dictionary();
        String error;
        loaded = load_ability_defaults(script, saved, error);
        draft = saved.duplicate(true);
        const String path = script->get_path();
        file->set_text(path.is_empty() ? "Unsaved ability script" : path);
        file->set_tooltip_text(path);
        for (int index = 0; index < static_cast<int>(rows.size()); ++index) {
            refresh_row(index);
        }
        save_button->set_disabled(true);
        show_status(error);
        popup_centered_clamped(Vector2i(ui::pixels(this, 655), ui::pixels(this, 666)), 0.88);
        set_process_input(true);
        if (loaded) {
            rows.front().trigger->grab_focus();
        } else {
            cancel_button->grab_focus();
        }
    }

    /// Close the form on Back, leaving the shared picker in charge if it is open.
    void _input(const Ref<InputEvent> &event) override {
        const auto *picker = gameplay_tags::find_gameplay_tag_picker();
        if (is_visible() && (!picker || !picker->is_visible()) && event.is_valid() &&
                (event->is_action_pressed("ui_cancel") || event->is_action_pressed("ui_close_dialog"))) {
            set_input_as_handled();
            close();
        }
    }
};

/// The single editor window used from the Inspector and open graphs.
AbilityDefaultsWindow *active_window = nullptr;

/// Give saved ability scripts the same Inspector action regardless of language.
class AbilityDefaultsInspector final : public EditorInspectorPlugin {
    GDCLASS(AbilityDefaultsInspector, EditorInspectorPlugin)

protected:
    /// This plugin only augments Godot's Inspector.
    static void _bind_methods() {}

public:
    bool _can_handle(Object *object) const override {
        return is_ability_defaults_script(selected_script(object));
    }

    /// Add the same entry for a Script asset or a node that owns one.
    void _parse_begin(Object *object) override {
        const Ref<Script> script = selected_script(object);
        if (!is_ability_defaults_script(script)) {
            return;
        }
        auto *edit = ui::button(nullptr, "Ability Defaults", "Edit the gameplay tags for this ability script.");
        edit->set_name("InspectorAbilityDefaults");
        if (!is_saved_ability_defaults_script(script)) {
            edit->set_disabled(true);
            ui::tooltip(edit, "Save the ability as a script file so Godot can assign its resource UID.");
        }
        edit->connect("pressed", callable_mp_static(open_ability_defaults_editor).bind(script, edit));
        add_custom_control(edit);
    }
};

/// Install the Inspector extension and own the editor window.
class AbilityDefaultsEditorPlugin final : public EditorPlugin {
    GDCLASS(AbilityDefaultsEditorPlugin, EditorPlugin)

    Ref<AbilityDefaultsInspector> inspector; ///< Registered while the plugin is in the tree.

protected:
    static void _bind_methods() {}

    /// Set up and remove the native editor surfaces at the standard plugin boundary.
    void _notification(int what) {
        if (what == NOTIFICATION_ENTER_TREE) {
            active_window = memnew(AbilityDefaultsWindow);
            active_window->configure();
            add_child(active_window);
            inspector.instantiate();
            add_inspector_plugin(inspector);
        } else if (what == NOTIFICATION_EXIT_TREE) {
            remove_inspector_plugin(inspector);
            inspector.unref();
            active_window = nullptr;
        }
    }
};

} // namespace

bool is_ability_defaults_script(const Ref<Script> &script) {
    return script.is_valid() && ClassDB::is_parent_class(script->get_instance_base_type(), "GameplayAbility");
}

bool is_saved_ability_defaults_script(const Ref<Script> &script) {
    return !get_ability_defaults_authoring_uid(script).is_empty();
}

void open_ability_defaults_editor(const Ref<Script> &script, Control *anchor) {
    if (active_window && is_ability_defaults_script(script)) {
        const String uid = get_ability_defaults_authoring_uid(script);
        if (!uid.is_empty()) {
            active_window->open(script, uid, anchor);
        }
    }
}

} // namespace crimson::abilities
#endif

namespace crimson::abilities {

void register_ability_defaults_editor(godot::ModuleInitializationLevel level) {
#ifdef DEBUG_ENABLED
    if (level == godot::MODULE_INITIALIZATION_LEVEL_EDITOR) {
        GDREGISTER_INTERNAL_CLASS(AbilityDefaultsWindow);
        GDREGISTER_INTERNAL_CLASS(AbilityDefaultsInspector);
        GDREGISTER_INTERNAL_CLASS(AbilityDefaultsEditorPlugin);
        godot::EditorPlugins::add_by_type<AbilityDefaultsEditorPlugin>();
    }
#else
    (void)level;
#endif
}

void unregister_ability_defaults_editor(godot::ModuleInitializationLevel level) {
#ifdef DEBUG_ENABLED
    if (level == godot::MODULE_INITIALIZATION_LEVEL_EDITOR) {
        godot::EditorPlugins::remove_by_type<AbilityDefaultsEditorPlugin>();
    }
#else
    (void)level;
#endif
}

} // namespace crimson::abilities
