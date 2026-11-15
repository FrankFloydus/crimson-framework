// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#include "register_gameplay_tags_editor.hpp"

#ifdef DEBUG_ENABLED

#include "editor/ui/editor_controls.hpp"
#include "editor/ui/editor_spacing.hpp"
#include "gameplay_tag_picker.hpp"
#include "modules/gameplay_tags/bindings/gameplay_tag_resources.hpp"

#include <godot_cpp/classes/button.hpp>
#include <godot_cpp/classes/container.hpp>
#include <godot_cpp/classes/editor_interface.hpp>
#include <godot_cpp/classes/editor_inspector_plugin.hpp>
#include <godot_cpp/classes/editor_plugin.hpp>
#include <godot_cpp/classes/editor_plugin_registration.hpp>
#include <godot_cpp/classes/editor_property.hpp>
#include <godot_cpp/classes/editor_undo_redo_manager.hpp>
#include <godot_cpp/classes/font.hpp>
#include <godot_cpp/classes/h_box_container.hpp>
#include <godot_cpp/classes/margin_container.hpp>
#include <godot_cpp/classes/panel_container.hpp>
#include <godot_cpp/classes/style_box_flat.hpp>
#include <godot_cpp/classes/texture2d.hpp>
#include <godot_cpp/classes/viewport.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/callable_method_pointer.hpp>

#include <algorithm>
#include <cmath>
#include <vector>

namespace crimson::gameplay_tags {
namespace {

using namespace godot;
using TagResource = bindings::GameplayTag;
using ContainerResource = bindings::GameplayTagContainer;
namespace ui = crimson::editor_ui;

/// Lifetime is owned by the native editor plugin.
ObjectID active_picker;

/// Disable unavailable Inspector commands and remove them from keyboard traversal.
void set_locked(Button *button, bool locked) {
    button->set_disabled(locked);
    button->set_focus_mode(locked ? Control::FOCUS_NONE : Control::FOCUS_ALL);
    button->set_default_cursor_shape(locked ? Control::CURSOR_FORBIDDEN : Control::CURSOR_POINTING_HAND);
}

/// Draw a compact square tag chip with the shared editor colors.
Ref<StyleBoxFlat> chip_frame(Control *host) {
    return ui::box(ui::color(ui::Surface::RAISED), ui::color(ui::Surface::OUTLINE),
            0, 0, host->get_theme_default_base_scale());
}

/// Keep every action state square, including focus and pointer hover.
void square_action(Button *button, int horizontal_padding) {
    const double scale = button->get_theme_default_base_scale();
    const Color transparent(0, 0, 0, 0);
    const auto normal = ui::box(transparent, transparent, horizontal_padding, 2, scale);
    const auto hover = ui::box(ui::color(ui::Surface::HOVER), transparent, horizontal_padding, 2, scale);
    const auto pressed = ui::box(ui::color(ui::Surface::SELECTED), transparent, horizontal_padding, 2, scale);
    button->add_theme_stylebox_override("normal", normal);
    button->add_theme_stylebox_override("disabled", normal);
    button->add_theme_stylebox_override("hover", hover);
    button->add_theme_stylebox_override("pressed", pressed);
    button->add_theme_stylebox_override("hover_pressed", pressed);
    button->add_theme_stylebox_override("focus", ui::box(transparent, ui::color(ui::Surface::ACCENT),
            horizontal_padding, 2, scale));
}

/// Add one clipped, keyboard-focusable caption with a full accessible name.
Button *chip_caption(Node *row, const Callable &open, int horizontal_padding = 6) {
    auto *caption = ui::button(row, {}, {}, open, ui::ButtonStyle::QUIET);
    caption->set_clip_text(true);
    caption->set_text_overrun_behavior(TextServer::OVERRUN_TRIM_ELLIPSIS);
    caption->set_text_alignment(HORIZONTAL_ALIGNMENT_LEFT);
    caption->set_h_size_flags(Control::SIZE_EXPAND_FILL);
    caption->set_custom_minimum_size(Vector2(0, ui::pixels(row, 24)));
    square_action(caption, horizontal_padding);
    return caption;
}

/// Edit a single gameplay tag or an explicit set with detached replacements.
class GameplayTagInspectorProperty final : public EditorProperty {
    GDCLASS(GameplayTagInspectorProperty, EditorProperty)

    /// Horizontal constants for one chip while its text can shrink on narrow layouts.
    struct ChipWidth {
        Button *caption; ///< Text action that gets an adjustable width.
        double natural; ///< Full text width with the caption's padding.
        double fixed; ///< Remove action and the chip outline.
    };

    /// Picker owned by the editor plugin.
    ObjectID picker_id;
    /// True for a GameplayTagContainer property.
    bool multiple = false;
    /// Disables edits for Godot's read-only Inspector and remote resources.
    bool read_only = false;
    /// The stable edit action used as a focus fallback when chips change.
    Button *trigger = nullptr;
    /// Separates the Inspector's available width from FlowContainer's child minimum widths.
    Control *chip_area = nullptr;
    /// Stock Godot horizontal flow owns the tag chips and computes line breaks.
    Container *chips = nullptr;
    /// Current chip measurements used when the Inspector changes width.
    std::vector<ChipWidth> chip_widths;
    /// The last snapshot drawn for this value.
    PackedStringArray rendered;

    /// Read the currently assigned paths without changing a shared Resource.
    PackedStringArray read_value() {
        PackedStringArray paths;
        Object *owner = get_edited_object();
        if (!owner) {
            return paths;
        }
        const Variant value = owner->get(get_edited_property());
        if (multiple) {
            const Ref<ContainerResource> container = value;
            if (container.is_valid()) {
                for (const Variant &entry : container->get_tags()) {
                    const Ref<TagResource> tag = entry;
                    paths.append(String(tag->get_tag_name()));
                }
            }
        } else {
            const Ref<TagResource> tag = value;
            if (tag.is_valid() && tag->is_valid()) {
                paths.append(String(tag->get_tag_name()));
            }
        }
        return paths;
    }

    /// Return the dialog if its owning plugin is still active.
    GameplayTagEditorPicker *picker() const {
        return Object::cast_to<GameplayTagEditorPicker>(ObjectDB::get_instance(picker_id));
    }

    /// Dismiss a pending edit if this field becomes unavailable.
    void dismiss_picker() {
        if (auto *dialog = picker()) {
            dialog->dismiss_source(ObjectID(get_instance_id()));
        }
    }

    /// Stage one edit against the value and object currently displayed by the Inspector.
    void open_picker(const String &reveal) {
        auto *dialog = picker();
        Object *owner = get_edited_object();
        if (read_only || !dialog || !owner) {
            return;
        }
        const PackedStringArray previous = read_value();
        const String focus_path = reveal.is_empty() && !multiple && !previous.is_empty() ? previous[0] : reveal;
        Control *focused = get_viewport()->gui_get_focus_owner();
        if (!focused || !is_ancestor_of(focused)) {
            focused = trigger;
        }
        dialog->open(previous, multiple, focus_path, ObjectID(get_instance_id()), ObjectID(focused->get_instance_id()),
                ObjectID(trigger->get_instance_id()),
                callable_mp(this, &GameplayTagInspectorProperty::submit_value)
                        .bind(owner->get_instance_id(), get_edited_property(), previous));
    }

    /// Apply a draft only if its target field and source value still match.
    bool submit_value(const PackedStringArray &paths, uint64_t previous_owner, const StringName &property,
            const PackedStringArray &previous) {
        Object *owner = get_edited_object();
        if (read_only || !owner || owner->get_instance_id() != previous_owner ||
                get_edited_property() != property || read_value() != previous) {
            return false;
        }
        if (paths == previous) {
            return true;
        }
        Variant value;
        if (multiple) {
            Ref<ContainerResource> next;
            next.instantiate();
            for (const String &path : paths) {
                const auto tag = TagResource::from_name(StringName(path));
                if (tag.is_null()) {
                    return false;
                }
                next->add(tag);
            }
            value = next;
        } else if (!paths.is_empty()) {
            const auto tag = TagResource::from_name(StringName(paths[0]));
            if (tag.is_null()) {
                return false;
            }
            value = tag;
        }
        auto *history = EditorInterface::get_singleton()->get_editor_undo_redo();
        const bool undo_disabled = owner->has_method("_dont_undo_redo") && bool(owner->call("_dont_undo_redo"));
        const bool separate_action = history && !owner->is_class("MultiNodeEdit") &&
                !owner->is_class("EditorDebuggerRemoteObjects") && !undo_disabled;
        if (separate_action) {
            // Keep discrete chip edits separate while using Godot's Inspector refresh and reference tracking.
            history->create_action(multiple ? "Edit Gameplay Tag Container" : "Edit Gameplay Tag",
                    UndoRedo::MERGE_DISABLE, owner);
        }
        emit_changed(property, value);
        if (separate_action) {
            history->commit_action();
        }
        callable_mp(this, &GameplayTagInspectorProperty::_update_property).call_deferred();
        return true;
    }

    /// Remove the exact displayed tag through the same native undo path as Apply.
    void remove_tag(const String &path) {
        Object *owner = get_edited_object();
        if (read_only || !owner) {
            return;
        }
        const PackedStringArray previous = read_value();
        PackedStringArray next = previous.duplicate();
        if (next.erase(path) && submit_value(next, owner->get_instance_id(), get_edited_property(), previous)) {
            trigger->call_deferred("grab_focus");
        }
    }

    /// Expose the flow's wrapped height to the property row without making the Inspector wider.
    void update_container_height() {
        chip_area->set_custom_minimum_size(Vector2(0, chips->get_combined_minimum_size().y));
    }

    /// Limit the width of each long caption to its allocated row, leaving the remove action visible.
    void fit_chips() {
        const Vector2 available = chip_area->get_size();
        for (const ChipWidth &measure : chip_widths) {
            const double fitted = std::min(measure.natural, std::max(0.0, double(available.x) - measure.fixed));
            const Vector2 minimum = measure.caption->get_custom_minimum_size();
            measure.caption->set_custom_minimum_size(Vector2(fitted, minimum.y));
        }
        chips->set_size(available);
    }

    /// Render current explicit tags as editable, independently removable chips.
    void rebuild_chips() {
        chip_widths.clear();
        while (chips->get_child_count()) {
            Node *old = chips->get_child(0);
            chips->remove_child(old);
            old->queue_free();
        }
        for (const String &path : rendered) {
            auto *chip = memnew(PanelContainer);
            chips->add_child(chip);
            chip->set_name("GameplayTagChip");
            chip->set_mouse_filter(Control::MOUSE_FILTER_IGNORE);
            const auto frame = chip_frame(chip);
            chip->add_theme_stylebox_override("panel", frame);
            auto *row = memnew(HBoxContainer);
            chip->add_child(row);
            row->set_mouse_filter(Control::MOUSE_FILTER_IGNORE);
            row->add_theme_constant_override("separation", 0);
            auto *edit = chip_caption(row, callable_mp(this, &GameplayTagInspectorProperty::open_picker).bind(path), 4);
            edit->set_name("GameplayTagChipEdit");
            edit->set_text(path);
            ui::tooltip(edit, path + String("\nOpen the tag picker at this tag."));
            edit->set_accessibility_name(String("Edit tag ") + path);
            set_locked(edit, read_only);
            auto *remove = ui::icon_button(row, "close", String("Remove ") + path + " from this container.",
                    callable_mp(this, &GameplayTagInspectorProperty::remove_tag).bind(path), ui::ButtonStyle::QUIET);
            remove->set_name("GameplayTagChipRemove");
            remove->set_custom_minimum_size(Vector2(ui::pixels(row, 20), ui::pixels(row, 24)));
            square_action(remove, 0);
            set_locked(remove, read_only);

            const Ref<Font> font = edit->get_theme_font("font");
            const double text_width = std::ceil(font->get_string_size(path, HORIZONTAL_ALIGNMENT_LEFT, -1,
                    edit->get_theme_font_size("font_size")).x);
            const double natural = text_width + edit->get_theme_stylebox("normal")->get_minimum_size().x;
            chip_widths.push_back({edit, natural, remove->get_combined_minimum_size().x + frame->get_minimum_size().x});
        }
        trigger->set_text(rendered.is_empty() ? "Add Tags" : String());
        trigger->set_button_icon(rendered.is_empty() ? ui::icon(trigger, "add") : Ref<Texture2D>());
        fit_chips();
    }

    /// Create the clickable field surface underneath an input-transparent wrapping chip layout.
    void build_container(const Ref<Theme> &theme, const Callable &open) {
        auto *field = memnew(PanelContainer);
        field->set_name("GameplayTagContainerField");
        field->set_theme(theme);
        field->set_mouse_filter(Control::MOUSE_FILTER_IGNORE);
        add_child(field);
        set_bottom_editor(field);
        field->add_theme_stylebox_override("panel", ui::box(ui::color(ui::Surface::HEADER), ui::color(ui::Surface::OUTLINE),
                0, 0, field->get_theme_default_base_scale()));

        trigger = ui::button(field, "Add Tags", "Choose which tags belong to this container.", open, ui::ButtonStyle::QUIET);
        trigger->set_text_alignment(HORIZONTAL_ALIGNMENT_LEFT);
        trigger->set_icon_alignment(HORIZONTAL_ALIGNMENT_LEFT);
        trigger->set_button_icon(ui::icon(trigger, "add"));
        trigger->set_accessibility_name("Add gameplay tags");
        square_action(trigger, 8);

        auto *insets = memnew(MarginContainer);
        field->add_child(insets);
        insets->set_mouse_filter(Control::MOUSE_FILTER_IGNORE);
        const int horizontal = ui::pixels(insets, ui::spacing::xs);
        const int vertical = ui::pixels(insets, ui::spacing::sm);
        insets->add_theme_constant_override("margin_left", horizontal);
        insets->add_theme_constant_override("margin_right", horizontal);
        insets->add_theme_constant_override("margin_top", vertical);
        insets->add_theme_constant_override("margin_bottom", vertical);
        chip_area = memnew(Control);
        insets->add_child(chip_area);
        chip_area->set_mouse_filter(Control::MOUSE_FILTER_IGNORE);
        chip_area->set_clip_contents(true);
        const Variant flow = ClassDB::instantiate("HFlowContainer");
        chips = Object::cast_to<Container>(flow.get_validated_object());
        ERR_FAIL_NULL(chips);
        chip_area->add_child(chips);
        chips->set_name("GameplayTagChips");
        chips->set_mouse_filter(Control::MOUSE_FILTER_IGNORE);
        chips->set_anchors_and_offsets_preset(Control::PRESET_FULL_RECT);
        for (const char *gap : {"h_separation", "v_separation"}) {
            chips->add_theme_constant_override(gap, ui::pixels(chips, ui::spacing::xs));
        }
        chip_area->connect("resized", callable_mp(this, &GameplayTagInspectorProperty::fit_chips));
        chips->connect("minimum_size_changed", callable_mp(this, &GameplayTagInspectorProperty::update_container_height));
    }

protected:
    /// Editor-only rows have no game-facing bindings.
    static void _bind_methods() {}

    /// Drop the draft when Godot rebuilds the Inspector or changes the selected object.
    void _notification(int what) {
        if (what == NOTIFICATION_EXIT_TREE) {
            dismiss_picker();
        }
    }

public:
    /// Build one stable row and an Inspector slot for container chips.
    void configure(bool is_multiple, ObjectID shared_picker, const Ref<Theme> &theme) {
        multiple = is_multiple;
        picker_id = shared_picker;
        set_name(multiple ? "GameplayTagContainerProperty" : "GameplayTagProperty");
        const auto callback = callable_mp(this, &GameplayTagInspectorProperty::open_picker).bind(String());
        if (multiple) {
            build_container(theme, callback);
        } else {
            set_name_split_ratio(0.4f);
            auto *chip = memnew(PanelContainer);
            chip->set_theme(theme);
            add_child(chip);
            chip->add_theme_stylebox_override("panel", chip_frame(chip));
            trigger = chip_caption(chip, callback);
            trigger->set_button_icon(ui::icon(trigger, "chevron_down"));
            trigger->set_icon_alignment(HORIZONTAL_ALIGNMENT_RIGHT);
        }
        trigger->set_name("GameplayTagPickerTrigger");
        add_focusable(trigger);
        _update_property();
    }

    /// Repaint external property updates, Undo, Redo, and revert.
    void _update_property() override {
        const auto value = read_value();
        if (multiple) {
            if (rendered != value) {
                rendered = value;
                rebuild_chips();
            }
            return;
        }
        rendered = value;
        const String path = value.is_empty() ? String() : value[0];
        trigger->set_text(path.is_empty() ? "Choose Tag" : path);
        ui::tooltip(trigger, path.is_empty() ? "Choose a single tag from the project catalog." :
                path + String("\nOpen the tag picker to replace or clear this value."));
        trigger->set_accessibility_name(path.is_empty() ? "Choose gameplay tag" : String("Edit tag ") + path);
    }

    /// Reflect read-only state in every chip and abort any pending edit.
    void _set_read_only(bool value) override {
        if (read_only == value) {
            return;
        }
        read_only = value;
        if (read_only) {
            dismiss_picker();
        }
        set_locked(trigger, read_only);
        if (multiple) {
            rebuild_chips();
        }
    }
};

/// Install a dedicated row for either declared type, including null exported values.
class GameplayTagInspector final : public EditorInspectorPlugin {
    GDCLASS(GameplayTagInspector, EditorInspectorPlugin)

    /// Picker for this editor session.
    ObjectID picker;
    /// Shared Crimson editor theme scoped to the custom field controls.
    Ref<Theme> field_theme;

protected:
    /// The internal plugin has no game-facing bindings.
    static void _bind_methods() {}

public:
    /// Share the picker and its theme across all Inspector fields.
    void configure(GameplayTagEditorPicker *dialog) {
        picker = ObjectID(dialog->get_instance_id());
        field_theme = dialog->get_theme();
    }

    /// The declared field type identifies matches independently of the edited object.
    bool _can_handle(Object *owner) const override { return owner != nullptr; }

    /// Replace Godot's generic resource dropdown where the declared field is a gameplay tag type.
    bool _parse_property(Object *, Variant::Type type, const String &name, PropertyHint hint,
            const String &hint_text, BitField<PropertyUsageFlags>, bool) override {
        if (type != Variant::OBJECT || hint != PROPERTY_HINT_RESOURCE_TYPE) {
            return false;
        }
        const StringName declared(hint_text);
        const bool multiple = declared == ContainerResource::get_class_static();
        if (!multiple && declared != TagResource::get_class_static()) {
            return false;
        }
        auto *row = memnew(GameplayTagInspectorProperty);
        row->configure(multiple, picker, field_theme);
        add_property_editor(name, row);
        return true;
    }
};

/// Own one Inspector plugin and one reusable picker for the Godot editor's lifetime.
class GameplayTagAuthoringPlugin final : public EditorPlugin {
    GDCLASS(GameplayTagAuthoringPlugin, EditorPlugin)

    /// Native property editor bridge installed with Godot.
    Ref<GameplayTagInspector> inspector;

protected:
    /// The Godot editor owns this plugin and its picker window.
    static void _bind_methods() {}

    /// Install the typed rows and remove them before editor shutdown.
    void _notification(int what) {
        if (what == NOTIFICATION_ENTER_TREE) {
            auto *picker = memnew(GameplayTagEditorPicker);
            picker->configure();
            add_child(picker);
            active_picker = ObjectID(picker->get_instance_id());
            inspector.instantiate();
            inspector->configure(picker);
            add_inspector_plugin(inspector);
        } else if (what == NOTIFICATION_EXIT_TREE && inspector.is_valid()) {
            remove_inspector_plugin(inspector);
            inspector.unref();
            active_picker = ObjectID();
        }
    }
};

} // namespace

GameplayTagEditorPicker *find_gameplay_tag_picker() {
    return Object::cast_to<GameplayTagEditorPicker>(ObjectDB::get_instance(active_picker));
}

} // namespace crimson::gameplay_tags

#endif

namespace crimson::gameplay_tags {

void register_gameplay_tags_editor(godot::ModuleInitializationLevel level) {
#ifdef DEBUG_ENABLED
    if (level == godot::MODULE_INITIALIZATION_LEVEL_EDITOR) {
        GDREGISTER_INTERNAL_CLASS(GameplayTagEditorPicker);
        GDREGISTER_INTERNAL_CLASS(GameplayTagInspectorProperty);
        GDREGISTER_INTERNAL_CLASS(GameplayTagInspector);
        GDREGISTER_INTERNAL_CLASS(GameplayTagAuthoringPlugin);
        godot::EditorPlugins::add_by_type<GameplayTagAuthoringPlugin>();
    }
#else
    (void)level;
#endif
}

void unregister_gameplay_tags_editor(godot::ModuleInitializationLevel level) {
#ifdef DEBUG_ENABLED
    if (level == godot::MODULE_INITIALIZATION_LEVEL_EDITOR) {
        godot::EditorPlugins::remove_by_type<GameplayTagAuthoringPlugin>();
    }
#else
    (void)level;
#endif
}

} // namespace crimson::gameplay_tags
