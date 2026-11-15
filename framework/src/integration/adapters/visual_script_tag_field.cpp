// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#include "modules/visual_scripting/editor/visual_script_tag_field.hpp"

#ifdef DEBUG_ENABLED

#include "editor/ui/editor_controls.hpp"
#include "modules/gameplay_tags/bindings/gameplay_tag_resources.hpp"
#include "modules/gameplay_tags/editor/gameplay_tag_picker.hpp"
#include "modules/visual_scripting/visual_script_assets.hpp"

#include <godot_cpp/classes/button.hpp>
#include <godot_cpp/classes/texture2d.hpp>
#include <godot_cpp/variant/callable_method_pointer.hpp>

namespace crimson::visual_scripting {
namespace {

using namespace godot;
namespace tags = crimson::gameplay_tags;
namespace ui = crimson::editor_ui;
using TagResource = tags::bindings::GameplayTag;
using TagContainerResource = tags::bindings::GameplayTagContainer;

/// Test whether this field can select more than one explicit tag.
bool multiple_tags(TagFieldKind kind) {
    return kind == TagFieldKind::CONTAINER || kind == TagFieldKind::ARRAY;
}

/// Read an existing inline path or the path in a legacy saved Tag resource.
String stored_tag_path(const Variant &value) {
    if (value.get_type() == Variant::STRING || value.get_type() == Variant::STRING_NAME) {
        return String(value);
    }
    const Ref<TagResource> asset = value.get_type() == Variant::OBJECT ?
            Ref<TagResource>(value) : Ref<TagResource>(load_asset_reference(value, "GameplayTag"));
    return asset.is_valid() && asset->is_valid() ? String(asset->get_tag_name()) : String();
}

/// Restore the current selection from the graph value, including external assets.
PackedStringArray stored_tag_paths(TagFieldKind kind, const Variant &stored) {
    PackedStringArray paths;
    if (!multiple_tags(kind)) {
        const String path = stored_tag_path(stored);
        if (!path.is_empty()) paths.append(path);
    } else if (stored.get_type() == Variant::PACKED_STRING_ARRAY) {
        paths = PackedStringArray(stored);
    } else if (kind == TagFieldKind::ARRAY && stored.get_type() == Variant::ARRAY) {
        for (const Variant &entry : Array(stored)) {
            const String path = stored_tag_path(entry);
            if (!path.is_empty() && !paths.has(path)) paths.append(path);
        }
    } else if (kind == TagFieldKind::CONTAINER) {
        const Ref<TagContainerResource> asset = stored.get_type() == Variant::OBJECT ?
                Ref<TagContainerResource>(stored) : Ref<TagContainerResource>(load_asset_reference(stored, "GameplayTagContainer"));
        if (asset.is_valid()) {
            for (const Variant &entry : asset->get_tags()) {
                const Ref<TagResource> tag = entry;
                if (tag.is_valid() && tag->is_valid()) paths.append(String(tag->get_tag_name()));
            }
        }
    }
    return paths;
}

/// Keep one picker caller alive with its canvas or Details field.
class VisualScriptTagField final : public Button {
    GDCLASS(VisualScriptTagField, Button)

    /// Current authored selection independent of any active picker draft.
    PackedStringArray paths;
    /// Describes the stored graph value.
    TagFieldKind kind = TagFieldKind::NONE;
    /// Retains the saved value displayed when the picker opened.
    Variant authored_value;
    /// Submits to the graph's existing validation, undo, and save path.
    std::function<bool(const Variant &, const Variant &)> commit;

    /// Show the path or count on the button and list exact entries on hover.
    void refresh_label() {
        if (paths.is_empty()) {
            set_text(multiple_tags(kind) ? "Choose Tags" : "Choose Tag");
            set_tooltip_text("Choose a default from the project Gameplay Tags catalog.");
        } else {
            set_text(paths.size() == 1 ? paths[0] : String::num_int64(paths.size()) + " tags");
            set_tooltip_text(String("\n").join(paths));
        }
    }

    /// Begin a shared picker draft with keyboard focus returning to the input.
    void open_picker() {
        auto *picker = tags::find_gameplay_tag_picker();
        if (!picker) return;
        const ObjectID source(get_instance_id());
        picker->open(paths, multiple_tags(kind), paths.is_empty() ? String() : paths[0],
                source, source, source, callable_mp(this, &VisualScriptTagField::submit));
    }

    /// Apply the selection only if the original edit is still accepted.
    bool submit(const PackedStringArray &next) {
        if (!commit || !is_visible_in_tree() || is_queued_for_deletion()) return false;
        if (next == paths && (authored_value.get_type() != Variant::NIL || !multiple_tags(kind))) return true;
        Variant value;
        if (multiple_tags(kind)) {
            value = next;
        } else if (!next.is_empty() || kind == TagFieldKind::NAME) {
            value = next.is_empty() ? StringName() : StringName(next[0]);
        }
        if (!commit(value, authored_value)) return false;
        paths = next.duplicate();
        authored_value = value;
        refresh_label();
        return true;
    }

protected:
    /// This composition is internal to the editor.
    static void _bind_methods() {}

    /// Cancel an orphaned draft when the graph changes or closes.
    void _notification(int what) {
        if (what == NOTIFICATION_EXIT_TREE || (what == NOTIFICATION_VISIBILITY_CHANGED && !is_visible_in_tree())) {
            if (auto *picker = tags::find_gameplay_tag_picker()) {
                picker->dismiss_source(ObjectID(get_instance_id()));
            }
        }
    }

public:
    /// Configure the button with its saved value and a detached commit callback.
    void configure(TagFieldKind type, const Variant &stored,
            const std::function<bool(const Variant &, const Variant &)> &change) {
        kind = type;
        paths = stored_tag_paths(type, stored);
        authored_value = stored;
        commit = change;
        set_name("VisualScriptTagDefault");
        set_text_alignment(HORIZONTAL_ALIGNMENT_LEFT);
        set_clip_text(true);
        set_text_overrun_behavior(TextServer::OVERRUN_TRIM_ELLIPSIS);
        set_button_icon(ui::icon(this, "chevron_down"));
        set_icon_alignment(HORIZONTAL_ALIGNMENT_RIGHT);
        set_accessibility_name(multiple_tags(kind) ? "Choose Gameplay Tags" : "Choose Gameplay Tag");
        set_default_cursor_shape(Control::CURSOR_POINTING_HAND);
        refresh_label();
        connect("pressed", callable_mp(this, &VisualScriptTagField::open_picker));
    }
};

} // namespace

TagFieldKind tag_field_kind(const Dictionary &pin, const Dictionary &node) {
    switch (gameplay_tag_literal_type(pin)) {
        case GameplayTagLiteralType::TAG: return TagFieldKind::TAG;
        case GameplayTagLiteralType::CONTAINER: return TagFieldKind::CONTAINER;
        case GameplayTagLiteralType::TAG_ARRAY: return TagFieldKind::ARRAY;
        default: break;
    }
    if (int64_t(pin.get("type", Variant::NIL)) != Variant::STRING_NAME ||
            StringName(pin.get("id", StringName())) != StringName("arg_0") ||
            StringName(node.get("kind", StringName())) != StringName("call_method")) return TagFieldKind::NONE;
    const Dictionary member = node.get("member", Dictionary());
    if (!String(member.get("script_path", String())).is_empty()) return TagFieldKind::NONE;
    const StringName owner = member.get("class", StringName());
    const StringName method = member.get("name", StringName());
    if (owner == StringName("GameplayTag") && (method == StringName("find") || method == StringName("from_name"))) {
        return TagFieldKind::NAME;
    }
    if (owner == StringName("GameplayTagCatalog") && (method == StringName("find") ||
            method == StringName("remove_project_tag") || method == StringName("rename_project_tag"))) {
        return TagFieldKind::NAME;
    }
    return TagFieldKind::NONE;
}

Button *tag_field(Node *parent, TagFieldKind kind, const Variant &stored,
        const std::function<bool(const Variant &, const Variant &)> &change) {
    auto *field = memnew(VisualScriptTagField);
    parent->add_child(field);
    field->configure(kind, stored, change);
    return field;
}

void register_visual_script_tag_field() {
    GDREGISTER_INTERNAL_CLASS(VisualScriptTagField);
}

} // namespace crimson::visual_scripting

#endif
