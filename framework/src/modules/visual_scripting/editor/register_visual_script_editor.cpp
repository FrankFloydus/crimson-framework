// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#include "modules/visual_scripting/editor/register_visual_script_editor.hpp"

#ifdef DEBUG_ENABLED

#include "modules/visual_scripting/editor/visual_script_custom_code.hpp"
#include "modules/visual_scripting/editor/visual_script_debug_navigation.hpp"
#include "modules/visual_scripting/editor/visual_script_help.hpp"
#include "modules/visual_scripting/editor/visual_script_window.hpp"

#include <godot_cpp/classes/button.hpp>
#include <godot_cpp/classes/display_server.hpp>
#include <godot_cpp/classes/editor_inspector_plugin.hpp>
#include <godot_cpp/classes/editor_interface.hpp>
#include <godot_cpp/classes/editor_plugin.hpp>
#include <godot_cpp/classes/editor_plugin_registration.hpp>
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/image.hpp>
#include <godot_cpp/classes/image_texture.hpp>
#include <godot_cpp/classes/scene_tree.hpp>
#include <godot_cpp/classes/v_box_container.hpp>
#include <godot_cpp/classes/tree.hpp>
#include <godot_cpp/classes/tree_item.hpp>
#include <godot_cpp/classes/window.hpp>
#include <godot_cpp/variant/callable_method_pointer.hpp>
#include <vector>

namespace crimson::visual_scripting {
namespace {

using namespace godot;

/// The editor plugin owns the single graph workspace in this editor session.
VisualScriptWindow *active_workspace = nullptr;
/// The stock editor uses this title to select the plugin's main screen.
constexpr char visual_script_screen[] = "Visual Script";

/// Identify the stock Scene dock script action without taking over its click behavior.
constexpr int scene_script_button = 2;
/// The same resource-class art configured in the GDExtension descriptor.
constexpr char visual_script_icon_path[] = "res://addons/crimson-framework/images/visual_script.png";

/// Open a script from the native script-language callback or the Inspector.
void open_script(const Ref<VisualScript> &script) {
    if (active_workspace && script.is_valid()) {
        active_workspace->open(script);
    }
}

/// Expose a graph entry point for selected script resources and their owning nodes.
class VisualScriptInspector final : public EditorInspectorPlugin {
    GDCLASS(VisualScriptInspector, EditorInspectorPlugin)

    /// Resolve the selected graph or a node's attached graph.
    static Ref<VisualScript> graph_for(Object *object) {
        if (auto *resource = Object::cast_to<VisualScript>(object)) {
            return Ref<VisualScript>(resource);
        }
        if (auto *node = Object::cast_to<Node>(object)) {
            return node->get_script();
        }
        return {};
    }

protected:
    /// This is an editor-only integration with no script bindings.
    static void _bind_methods() {}

public:
    /// Only show this command for objects with a Crimson visual script.
    bool _can_handle(Object *object) const override {
        return graph_for(object).is_valid();
    }

    /// Add a direct graph-opening button without exposing raw storage.
    void _parse_begin(Object *object) override {
        const Ref<VisualScript> script = graph_for(object);
        if (script.is_null()) {
            return;
        }
        auto *button = memnew(Button);
        button->set_text("Edit Visual Script");
        button->set_tooltip_text("Open the attached Crimson visual script in the graph editor.");
        button->connect("pressed", callable_mp_static(open_script).bind(script));
        add_custom_control(button);
    }

    /// Raw graph storage belongs to the graph editor.
    bool _parse_property(Object *object, Variant::Type, const String &name, PropertyHint, const String &,
            BitField<PropertyUsageFlags>, bool) override {
        return Object::cast_to<VisualScript>(object) && name == "graph";
    }
};

/// Integrate the graph editor into the native Inspector and editor save lifecycle.
class VisualScriptEditorPlugin final : public EditorPlugin {
    GDCLASS(VisualScriptEditorPlugin, EditorPlugin)

    /// Inspector registered for the current editor session.
    Ref<VisualScriptInspector> inspector;
    /// Bridges native Godot breakpoint sessions to the graph workspace.
    Ref<VisualScriptEditorDebugger> debugger;
    /// The stock Scene dock's public Tree, if this engine layout exposes the expected controls.
    ObjectID scene_tree_id;
    /// Keep the custom texture stable across native redraws and match the stock icon footprint.
    Ref<ImageTexture> script_icon;
    /// Draw can recur during one display pass. Schedule at most one update after it.
    bool scene_icon_update_pending = false;

    /// Activate the stock main screen from a script open or debugger navigation.
    void reveal_workspace() {
        EditorInterface::get_singleton()->set_main_screen_editor(visual_script_screen);
    }

    /// Find the Scene dock only. FileSystem rows and other script buttons retain native presentation.
    void observe_scene_dock() {
        if (!is_inside_tree() || DisplayServer::get_singleton()->get_name() == "headless") return;
        const TypedArray<Node> docks = get_tree()->get_root()->find_children("*", "SceneTreeDock", true, false);
        if (docks.size() != 1 || !FileAccess::file_exists(visual_script_icon_path)) return;
        auto *dock = Object::cast_to<Node>(docks[0].get_validated_object());
        if (!dock) return;
        // Godot also nests a SceneTreeEditor in a picker dialog within the dock.
        Control *editor = nullptr;
        for (const Variant &candidate : dock->find_children("*", "SceneTreeEditor", true, false)) {
            auto *control = Object::cast_to<Control>(candidate.get_validated_object());
            if (!control) continue;
            for (Node *parent = control->get_parent(); parent; parent = parent->get_parent()) {
                if (parent == dock) {
                    if (editor) return;
                    editor = control;
                    break;
                }
                if (Object::cast_to<Window>(parent)) break;
            }
        }
        if (!editor) return;
        const TypedArray<Node> rows = editor->find_children("*", "Tree", false, false);
        if (rows.size() != 1) return;
        auto *tree = Object::cast_to<Tree>(rows[0].get_validated_object());
        if (!tree) return;
        // The editor integration can start before its addon art finishes the first import.
        Ref<Image> image;
        image.instantiate();
        if (image->load_png_from_buffer(FileAccess::get_file_as_bytes(visual_script_icon_path)) != OK) return;
        script_icon = ImageTexture::create_from_image(image);
        scene_tree_id = ObjectID(tree->get_instance_id());
        tree->connect("draw", callable_mp(this, &VisualScriptEditorPlugin::queue_scene_icons));
        tree->connect("tree_exiting", callable_mp(this, &VisualScriptEditorPlugin::forget_scene_dock), CONNECT_ONE_SHOT);
        queue_scene_icons();
    }

    /// Inspect freshly rebuilt stock rows after the Scene dock has finished drawing.
    void queue_scene_icons() {
        if (scene_icon_update_pending) return;
        scene_icon_update_pending = true;
        callable_mp(this, &VisualScriptEditorPlugin::refresh_scene_icons).call_deferred();
    }

    /// Clear the scheduled flag even if the dock is no longer present.
    void refresh_scene_icons() {
        scene_icon_update_pending = false;
        update_scene_icons(false);
    }

    /// Release the non-owning identity before Godot removes the dock's control tree.
    void forget_scene_dock() {
        scene_tree_id = ObjectID();
        script_icon.unref();
    }

    /// Restrict decoration to the stock script button of nodes with an attached Crimson graph.
    void update_scene_icons(bool restore) {
        if (uint64_t(scene_tree_id) == 0) return;
        auto *tree = Object::cast_to<Tree>(ObjectDB::get_instance(scene_tree_id));
        if (!tree || script_icon.is_null() || !tree->get_root()) return;
        auto *editor = Object::cast_to<Control>(tree->get_parent());
        auto *scene_root = EditorInterface::get_singleton()->get_edited_scene_root();
        if (!editor || (!restore && !scene_root)) return;
        const Ref<Texture2D> stock = editor->get_theme_icon("Script", "EditorIcons");
        if (stock.is_null()) return;
        if (const Vector2 size = stock->get_size(); !restore && size != script_icon->get_size()) {
            script_icon->set_size_override(Vector2i(size));
        }
        std::vector<TreeItem *> rows{tree->get_root()};
        while (!rows.empty()) {
            TreeItem *row = rows.back();
            rows.pop_back();
            for (TreeItem *child = row->get_first_child(); child; child = child->get_next()) rows.push_back(child);
            const int button = row->get_button_by_id(0, scene_script_button);
            if (button < 0) continue;
            const Ref<Texture2D> current = row->get_button(0, button);
            if (restore) {
                if (current == script_icon) row->set_button(0, button, stock);
                continue;
            }
            if (current != stock) continue;
            const Variant metadata = row->get_metadata(0);
            if (metadata.get_type() != Variant::NODE_PATH) continue;
            Node *node = scene_root->get_node_or_null(NodePath(metadata));
            if (!node || (node != scene_root && !scene_root->is_ancestor_of(node))) continue;
            const Ref<VisualScript> attached = node->get_script();
            if (attached.is_valid()) row->set_button(0, button, script_icon);
        }
    }

protected:
    /// This is an editor-only integration with no script bindings.
    static void _bind_methods() {}

    /// Install the graph workspace and its entry points while the plugin is active.
    void _notification(int what) {
        if (what == NOTIFICATION_ENTER_TREE) {
            auto *main_screen = EditorInterface::get_singleton()->get_editor_main_screen();
            active_workspace = memnew(VisualScriptWindow);
            active_workspace->set_v_size_flags(Control::SIZE_EXPAND_FILL);
            active_workspace->set_h_size_flags(Control::SIZE_EXPAND_FILL);
            active_workspace->hide();
            main_screen->add_child(active_workspace);
            active_workspace->build(callable_mp(this, &VisualScriptEditorPlugin::reveal_workspace));
            debugger.instantiate();
            debugger->attach(active_workspace);
            add_debugger_plugin(debugger);
            inspector.instantiate();
            add_inspector_plugin(inspector);
            set_graph_open_callback(open_script);
            callable_mp(this, &VisualScriptEditorPlugin::observe_scene_dock).call_deferred();
        } else if (what == NOTIFICATION_EXIT_TREE) {
            auto *tree = uint64_t(scene_tree_id) != 0 ? Object::cast_to<Tree>(ObjectDB::get_instance(scene_tree_id)) : nullptr;
            if (tree && tree->is_inside_tree() && !tree->is_queued_for_deletion()) {
                tree->disconnect("draw", callable_mp(this, &VisualScriptEditorPlugin::queue_scene_icons));
                tree->disconnect("tree_exiting", callable_mp(this, &VisualScriptEditorPlugin::forget_scene_dock));
                update_scene_icons(true);
            }
            forget_scene_dock();
            set_graph_open_callback(nullptr);
            if (debugger.is_valid()) {
                debugger->detach();
                remove_debugger_plugin(debugger);
                debugger.unref();
            }
            if (inspector.is_valid()) {
                remove_inspector_plugin(inspector);
                inspector.unref();
            }
            if (active_workspace) {
                active_workspace->leave_workspace();
                active_workspace->queue_free();
                active_workspace = nullptr;
            }
        }
    }

public:
    /// Add Visual Script to the editor toolbar after the built-in screens.
    bool _has_main_screen() const override { return true; }

    /// Use the same name for its toolbar tab and programmatic selection.
    String _get_plugin_name() const override { return visual_script_screen; }

    /// Keep the asset and the main screen recognizable by the same icon.
    Ref<Texture2D> _get_plugin_icon() const override {
        Ref<Image> image;
        image.instantiate();
        const PackedByteArray bytes = FileAccess::get_file_as_bytes(visual_script_icon_path);
        if (bytes.is_empty() || image->load_png_from_buffer(bytes) != OK) return {};
        Ref<ImageTexture> icon = ImageTexture::create_from_image(image);
        const int size = static_cast<int>(16 * EditorInterface::get_singleton()->get_editor_scale());
        icon->set_size_override(Vector2i(size, size));
        return icon;
    }

    /// Preserve the current script and draft when another editor screen is selected.
    void _make_visible(bool visible) override {
        if (!active_workspace) return;
        if (!visible) active_workspace->leave_workspace();
        active_workspace->set_visible(visible);
        if (visible) active_workspace->enter_workspace();
    }

    /// Register saved visual breakpoints before the game executes scene startup.
    PackedStringArray _get_breakpoints() const override {
        return debugger.is_valid() ? debugger->launch_breakpoints() : PackedStringArray();
    }

    /// Include an edited script in editor-wide Save All.
    void _save_external_data() override {
        if (active_workspace) {
            active_workspace->save_external_data();
        }
    }

    /// Save completed graph edits even when Godot's save-before-running preference is off.
    bool _build() override {
        return !active_workspace || active_workspace->save_before_run();
    }

    /// Include outstanding graph edits in Godot's quit warning.
    String _get_unsaved_status(const String &scene_path) const override {
        return scene_path.is_empty() && active_workspace ? active_workspace->unsaved_status() : String();
    }
};

}

void register_visual_script_editor(ModuleInitializationLevel level) {
    if (level == MODULE_INITIALIZATION_LEVEL_EDITOR) {
        register_visual_script_canvas();
        GDREGISTER_INTERNAL_CLASS(VisualScriptEditorDebugger);
        GDREGISTER_INTERNAL_CLASS(VisualScriptCustomCode);
        GDREGISTER_INTERNAL_CLASS(VisualScriptWindow);
        GDREGISTER_INTERNAL_CLASS(VisualScriptInspector);
        GDREGISTER_INTERNAL_CLASS(VisualScriptEditorPlugin);
        EditorPlugins::add_by_type<VisualScriptEditorPlugin>();
    }
}

void unregister_visual_script_editor(ModuleInitializationLevel level) {
    if (level == MODULE_INITIALIZATION_LEVEL_EDITOR) {
        EditorPlugins::remove_by_type<VisualScriptEditorPlugin>();
        clear_visual_script_help();
    }
}

}

#else

namespace crimson::visual_scripting {

void register_visual_script_editor(godot::ModuleInitializationLevel) {}

void unregister_visual_script_editor(godot::ModuleInitializationLevel) {}

}

#endif
