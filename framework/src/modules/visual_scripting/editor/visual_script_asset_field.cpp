// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#include "modules/visual_scripting/editor/visual_script_asset_field.hpp"

#ifdef DEBUG_ENABLED

#include "editor/ui/editor_controls.hpp"
#include "editor/ui/editor_graph.hpp"
#include "editor/ui/editor_spacing.hpp"
#include "modules/visual_scripting/visual_script_assets.hpp"

#include <godot_cpp/classes/button.hpp>
#include <godot_cpp/classes/class_db_singleton.hpp>
#include <godot_cpp/classes/display_server.hpp>
#include <godot_cpp/classes/editor_file_dialog.hpp>
#include <godot_cpp/classes/editor_file_system.hpp>
#include <godot_cpp/classes/editor_interface.hpp>
#include <godot_cpp/classes/h_box_container.hpp>
#include <godot_cpp/classes/menu_button.hpp>
#include <godot_cpp/classes/panel_container.hpp>
#include <godot_cpp/classes/popup_menu.hpp>
#include <godot_cpp/classes/resource_loader.hpp>
#include <godot_cpp/classes/texture_rect.hpp>
#include <godot_cpp/classes/theme.hpp>
#include <godot_cpp/variant/callable_method_pointer.hpp>

#include <set>

namespace crimson::visual_scripting {

godot::String dropped_project_file(const godot::Variant &data) {
    using namespace godot;
    if (data.get_type() != Variant::DICTIONARY) return {};
    const Dictionary payload = data;
    if (String(payload.get("type", String())) != "files") return {};
    const PackedStringArray files = payload.get("files", PackedStringArray());
    return files.size() == 1 && files[0].begins_with("res://") ? files[0] : String();
}

namespace {

using namespace godot;
namespace ui = crimson::editor_ui;

/// Attach an owned child without retaining a detached control.
template <typename T>
T *add(Node *parent) {
    auto *child = memnew(T);
    parent->add_child(child);
    return child;
}

/// Use the stock editor collection for all native asset icons, including scripts and scenes.
Ref<Texture2D> resource_icon(const StringName &type) {
    const Ref<Theme> theme = EditorInterface::get_singleton()->get_editor_theme();
    if (theme.is_null()) return {};
    for (const StringName &candidate : {type, StringName("Resource"), StringName("Object")}) {
        if (theme->has_icon(candidate, "EditorIcons")) return theme->get_icon(candidate, "EditorIcons");
    }
    return {};
}

/// Show a fixed size preview card using Godot's asynchronous editor preview service.
class VisualScriptPreloadPreview final : public TextureRect {
    GDCLASS(VisualScriptPreloadPreview, TextureRect)

    /// The resolved UID or path of the saved resource displayed on this card.
    String asset_path;
    /// Keep an icon visible while a thumbnail is pending or when Godot has no preview.
    Ref<Texture2D> fallback;
    /// Ignore a callback if the native preview request has since been superseded.
    int64_t request = 0;

    /// Resolve the Godot editor service without adding it to the runtime binding profile.
    static Object *preview_service() {
        const Variant service = EditorInterface::get_singleton()->call("get_resource_previewer");
        return service.get_validated_object();
    }

    /// Ask the editor preview service for stock previews without loading or instantiating here.
    void queue_preview() {
        if (auto *previewer = preview_service()) {
            previewer->call("queue_resource_preview", asset_path, this, StringName("_receive_preview"), ++request);
        }
    }

    /// Regenerate the preview when the native editor invalidates this asset.
    void invalidated(const String &path) {
        if (path == asset_path && is_inside_tree() && !is_queued_for_deletion()) queue_preview();
    }

    /// Apply stock thumbnail results to the still-live card for the same asset request.
    void receive_preview(const String &path, const Ref<Texture2D> &preview,
            const Ref<Texture2D> &thumbnail, int64_t generation) {
        if (path != asset_path || request != generation || !is_inside_tree() || is_queued_for_deletion()) return;
        const Ref<Texture2D> image = preview.is_valid() ? preview : thumbnail;
        set_texture(image.is_valid() ? image : fallback);
        set_stretch_mode(image.is_valid() ? STRETCH_KEEP_ASPECT_CENTERED : STRETCH_KEEP_CENTERED);
    }

protected:
    /// Make the native asynchronous preview callback addressable only on this private control.
    static void _bind_methods() {
        ClassDB::bind_method(D_METHOD("_receive_preview", "path", "preview", "thumbnail", "request"),
                &VisualScriptPreloadPreview::receive_preview);
    }

public:
    /// Paint the icon immediately and request a thumbnail only for assets the editor can preview.
    void build(const String &path, const StringName &type, bool previewable) {
        asset_path = path;
        fallback = resource_icon(type);
        set_texture(fallback);
        set_stretch_mode(STRETCH_KEEP_CENTERED);
        set_expand_mode(EXPAND_IGNORE_SIZE);
        set_custom_minimum_size(Vector2(0, ui::pixels(this, 76)));
        set_mouse_filter(MOUSE_FILTER_IGNORE);
        if (!previewable || DisplayServer::get_singleton()->get_name() == "headless") return;

        if (auto *previewer = preview_service()) {
            previewer->connect("preview_invalidated", callable_mp(this, &VisualScriptPreloadPreview::invalidated));
            queue_preview();
        }
    }
};

/// A compact resource field with the editor's native project-file and Quick Open dialogs.
class VisualScriptAssetField final : public HBoxContainer {
    GDCLASS(VisualScriptAssetField, HBoxContainer)

    /// The field's basename and Quick Open button.
    Button *filename = nullptr;
    /// Type required by the input pin.
    StringName resource_type;
    /// Whether the default has a saved reference, even if the asset cannot be loaded.
    bool has_reference = false;
    /// Commit candidates through the graph's existing literal validation.
    std::function<void(const Ref<Resource> &)> on_change;

    /// Pass a selected path to the common saved-resource validation.
    void load_path(const String &path) {
        if (is_inside_tree() && !path.is_empty() && on_change) {
            const Ref<Resource> selected = ResourceLoader::get_singleton()->load(path);
            if (selected.is_valid()) {
                on_change(selected);
            }
        }
    }

    /// Filter Quick Open to types compatible with the resource pin.
    void quick_open() {
        EditorInterface::get_singleton()->popup_quick_open(
                callable_mp(this, &VisualScriptAssetField::load_path), TypedArray<StringName>::make(resource_type));
    }

    /// Keep an ordinary project file browser available alongside search.
    void browse() {
        browse_project_resource(this, resource_type, callable_mp(this, &VisualScriptAssetField::load_path));
    }

    /// Dispatch the field's resource menu commands.
    void menu_action(int64_t command) {
        switch (command) {
            case 0: quick_open(); break;
            case 1: browse(); break;
            case 2: if (is_inside_tree() && on_change) on_change(Ref<Resource>()); break;
        }
    }

protected:
    /// The field is private to the Visual Script workspace.
    static void _bind_methods() {}

public:
    /// Build the filename field and its native menu.
    void build(const StringName &type, const String &help, const std::function<void(const Ref<Resource> &)> &change,
            bool inline_graph) {
        resource_type = type;
        on_change = change;
        set_tooltip_text(help);
        add_theme_constant_override("separation", ui::pixels(this, 3));
        filename = add<Button>(this);
        filename->set_name("GraphAssetName");
        filename->set_h_size_flags(SIZE_EXPAND_FILL);
        filename->set_custom_minimum_size(Vector2(ui::pixels(this, inline_graph ? 86 : 36), 0));
        filename->set_text_alignment(HORIZONTAL_ALIGNMENT_LEFT);
        filename->set_text_overrun_behavior(TextServer::OVERRUN_TRIM_ELLIPSIS);
        filename->set_clip_text(true);
        filename->set_accessibility_name("Choose a saved " + String(type));
        filename->set_mouse_filter(MOUSE_FILTER_PASS);
        filename->set_focus_mode(FOCUS_ALL);
        if (inline_graph) ui::style_graph_inline_control(filename, "GraphLiteralAsset");
        filename->connect("pressed", callable_mp(this, &VisualScriptAssetField::quick_open));

        auto *options = add<MenuButton>(this);
        options->set_name("GraphAssetOptions");
        options->set_flat(false);
        options->set_focus_mode(FOCUS_ALL);
        options->set_custom_minimum_size(Vector2(ui::pixels(this, 24), 0));
        if (inline_graph) ui::style_graph_inline_control(options, "GraphLiteralAsset");
        options->set_button_icon(get_theme_icon("GuiOptionArrow", "EditorIcons"));
        options->set_icon_alignment(HORIZONTAL_ALIGNMENT_CENTER);
        options->set_accessibility_name("Resource options");
        options->set_tooltip_text("Quick Load, Browse Files, or Clear the resource.");
        auto *menu = options->get_popup();
        menu->add_item("Quick Load...", 0);
        menu->add_item("Browse Files...", 1);
        menu->add_separator();
        menu->add_item("Clear", 2);
        menu->connect("about_to_popup", callable_mp_static(+[](PopupMenu *target, VisualScriptAssetField *field) {
            target->set_item_disabled(target->get_item_index(2), !field->has_reference);
        }).bind(menu, this));
        menu->connect("id_pressed", callable_mp(this, &VisualScriptAssetField::menu_action));
    }

    /// Display the saved resource using its class icon and basename.
    void display(const String &path, const StringName &type, bool available) {
        has_reference = !path.is_empty();
        filename->set_text(available ? path.get_file() :
                has_reference ? String("Missing: ") + path.get_file() : String("Choose asset..."));
        filename->set_tooltip_text(available ? path : has_reference ?
                String("The saved asset is unavailable: ") + path + "\nChoose a replacement or clear it." :
                get_tooltip_text());
        filename->set_button_icon(resource_icon(type));
    }

    /// Accept only one saved resource of a compatible FileSystem type.
    bool _can_drop_data(const Vector2 &, const Variant &data) const override {
        const String path = dropped_project_file(data);
        if (path.is_empty()) {
            return false;
        }
        const StringName type = EditorInterface::get_singleton()->get_resource_filesystem()->get_file_type(path);
        return type == resource_type || ClassDBSingleton::get_singleton()->is_parent_class(type, resource_type);
    }

    /// Apply a typed resource drop through the same path as the file dialogs.
    void _drop_data(const Vector2 &point, const Variant &data) override {
        if (_can_drop_data(point, data)) {
            load_path(dropped_project_file(data));
        }
    }
};

} // namespace

void browse_project_resource(Node *owner, const StringName &type, const Callable &chosen) {
    auto *dialog = add<EditorFileDialog>(owner);
    ui::style_file_dialog(dialog);
    dialog->set_title(type == StringName("Resource") ? String("Browse Project Assets") : String("Choose ") + String(type));
    dialog->set_access(FileDialog::ACCESS_RESOURCES);
    dialog->set_file_mode(FileDialog::FILE_MODE_OPEN_FILE);
    // Browsing a directory should not generate previews for unselected assets.
    dialog->set_display_mode(FileDialog::DISPLAY_LIST);

    // An empty type asks every registered loader, including script loaders that
    // only declare support for their specific Script subclass.
    const String filter_type = type == StringName("Resource") ? String() : String(type);
    const PackedStringArray extensions = ResourceLoader::get_singleton()->get_recognized_extensions_for_type(filter_type);
    std::set<String> unique;
    String filter;
    for (const String &extension : extensions) {
        if (!unique.insert(extension).second) continue;
        if (!filter.is_empty()) filter += ", ";
        filter += "*." + extension;
    }
    if (!filter.is_empty()) dialog->add_filter(filter, "Project assets");
    dialog->connect("file_selected", chosen, Object::CONNECT_DEFERRED);
    dialog->connect("file_selected", callable_mp(static_cast<Node *>(dialog), &Node::queue_free).unbind(1), Object::CONNECT_DEFERRED);
    dialog->connect("canceled", callable_mp(static_cast<Node *>(dialog), &Node::queue_free), Object::CONNECT_DEFERRED);
    dialog->popup_file_dialog();
}

Control *asset_field(Node *parent, const StringName &type, const String &help, const Variant &stored,
        const std::function<bool(const Ref<Resource> &)> &change, bool inline_graph, bool preload) {
    auto *field = add<VisualScriptAssetField>(parent);
    field->build(type, help, [field, change](const Ref<Resource> &selected) {
        if (change(selected) && field->is_inside_tree() && !field->is_queued_for_deletion()) {
            field->display(selected.is_valid() ? selected->get_path() : String(),
                    selected.is_valid() ? selected->get_class() : StringName("Resource"), selected.is_valid());
        }
    }, inline_graph);
    const String path = resolved_asset_reference_path(stored);
    if (preload) {
        const StringName indexed = path.is_empty() ? StringName() :
                EditorInterface::get_singleton()->get_resource_filesystem()->get_file_type(path);
        const bool available = !path.is_empty() && ResourceLoader::get_singleton()->exists(path) &&
                (indexed.is_empty() || ClassDBSingleton::get_singleton()->is_parent_class(indexed, type));
        field->display(path, indexed.is_empty() ? type : indexed, available);
    } else {
        const Ref<Resource> resource = load_asset_reference(stored, type);
        field->display(resource.is_valid() ? resource->get_path() : path, resource.is_valid() ? resource->get_class() : type,
                resource.is_valid());
    }
    return field;
}

void add_preload_preview(Node *parent, const Variant &stored, const StringName &asset_class) {
    const String path = resolved_asset_reference_path(stored);
    if (path.is_empty() || !ResourceLoader::get_singleton()->exists(path)) return;
    auto *panel = add<PanelContainer>(parent);
    panel->set_mouse_filter(Control::MOUSE_FILTER_IGNORE);
    auto border = ui::box(ui::color(ui::Surface::CANVAS), ui::color(ui::Surface::OUTLINE), 0, 0, panel->get_theme_default_base_scale());
    border->set_border_width_all(ui::pixels(panel, 1));
    border->set_corner_radius_all(ui::pixels(panel, ui::spacing::xs));
    panel->add_theme_stylebox_override("panel", border);
    auto *preview = add<VisualScriptPreloadPreview>(panel);
    preview->set_name("GraphAssetPreview");
    const auto *filesystem = EditorInterface::get_singleton()->get_resource_filesystem();
    const StringName indexed_type = filesystem->get_file_type(path);
    const StringName type = indexed_type.is_empty() ? asset_class : indexed_type;
    // The abstract AudioStream resource has no playback implementation for the stock waveform generator.
    preview->build(path, type, !indexed_type.is_empty() && type != StringName("AudioStream"));
}

void register_visual_script_asset_field() {
    GDREGISTER_INTERNAL_CLASS(VisualScriptAssetField);
    GDREGISTER_INTERNAL_CLASS(VisualScriptPreloadPreview);
}

} // namespace crimson::visual_scripting

#endif
