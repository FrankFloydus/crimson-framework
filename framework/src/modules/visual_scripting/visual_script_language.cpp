// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#include "modules/visual_scripting/visual_script_language.hpp"

#include "modules/visual_scripting/visual_script.hpp"
#include "modules/visual_scripting/visual_script_assets.hpp"
#include "modules/visual_scripting/visual_script_debugger.hpp"

#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/os.hpp>
#include <godot_cpp/classes/project_settings.hpp>
#include <godot_cpp/classes/resource_format_loader.hpp>
#include <godot_cpp/classes/resource_format_saver.hpp>
#include <godot_cpp/classes/resource_loader.hpp>
#include <godot_cpp/classes/resource_saver.hpp>
#include <godot_cpp/classes/script_language_extension.hpp>
#include <godot_cpp/classes/time.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include <atomic>
#include <filesystem>
#include <fstream>
#include <string>

namespace crimson::visual_scripting {
namespace {

using namespace godot;

/// File type shared by the language, resource loader, and saver.
constexpr char extension[] = "crimsonscript";
/// Active graph editor callback while the editor plug-in is registered.
GraphOpenCallback open_graph = nullptr;

/// Write a complete sibling before replacing the previous graph file.
Error publish_graph(const String &path, const String &source) {
    static std::atomic<uint64_t> next_write{0};
    const auto native_path = ProjectSettings::get_singleton()->globalize_path(path).utf8();
    const auto target = std::filesystem::u8path(native_path.get_data());
    auto temporary = target;
    temporary += ".tmp-" + std::to_string(OS::get_singleton()->get_process_id()) + "-" +
                 std::to_string(Time::get_singleton()->get_ticks_usec()) + "-" +
                 std::to_string(next_write.fetch_add(1, std::memory_order_relaxed));

    std::ofstream file(temporary, std::ios::binary);
    if (!file) {
        return ERR_FILE_CANT_OPEN;
    }
    const auto bytes = source.utf8();
    file.write(bytes.get_data(), bytes.length());
    file.close();

    std::error_code error;
    if (file) {
        std::filesystem::rename(temporary, target, error);
        if (!error) {
            return OK;
        }
    }
    std::filesystem::remove(temporary, error);
    return ERR_FILE_CANT_WRITE;
}

/// Integrate VisualScript with Godot's standard script creation dialog.
class VisualScriptLanguage final : public ScriptLanguageExtension {
    GDCLASS(VisualScriptLanguage, ScriptLanguageExtension)

protected:
    /// All registration uses Godot's ScriptLanguage virtual interface.
    static void _bind_methods() {}

public:
    /// Display the language in Godot's Attach Script dialog.
    String _get_name() const override { return "Crimson Visual Script"; }
    /// Identify the native Script resource class provided by the language.
    String _get_type() const override { return "VisualScript"; }
    /// Suggest the graph extension when creating a script file.
    String _get_extension() const override { return extension; }
    /// Initialize the stateless language adapter.
    void _init() override {}
    /// Stop the stateless language adapter.
    void _finish() override {}
    /// Return the extensions recognized by the script dialog.
    PackedStringArray _get_recognized_extensions() const override {
        PackedStringArray result;
        result.append(extension);
        return result;
    }
    /// Make a graph for the base selected in Godot's script dialog.
    Ref<Script> _make_template(const String &, const String &, const String &base) const override {
        Ref<VisualScript> script;
        script.instantiate();
        script->set_graph_base(StringName(base));
        return script;
    }
    /// Report whether a generated template is needed.
    bool _is_using_templates() override { return false; }
    /// Keep authored graphs as independent files.
    bool _supports_builtin_mode() const override { return false; }
    /// Offer native classes in the base-class picker.
    bool _can_inherit_from_file() const override { return false; }
    /// Validate the graph extension selected for the script.
    String _validate_path(const String &path) const override {
        return path.get_extension() == extension ? String() : String("Use the .crimsonscript extension.");
    }
    /// Use Crimson's graph workspace when a script is opened.
    bool _overrides_external_editor() override { return open_graph != nullptr; }
    /// Hand the selected script to the registered graph workspace.
    Error _open_in_external_editor(const Ref<Script> &script, int32_t, int32_t) override {
        const Ref<VisualScript> graph = script;
        if (graph.is_null() || !open_graph) {
            return ERR_UNAVAILABLE;
        }
        open_graph(graph);
        return OK;
    }
    /// Report the absence of named graph classes.
    bool _has_named_classes() const override { return false; }
    /// Report whether this adapter can declare a named global script class.
    bool _handles_global_class_type(const String &) const override { return false; }
    /// Return no global class declaration for graph files.
    Dictionary _get_global_class_name(const String &) const override { return {}; }
    /// Report whether the language supports built-in script help pages.
    bool _supports_documentation() const override { return false; }
    /// Graph events are authored in the graph editor.
    bool _can_make_function() const override { return false; }
    /// Return no generated text method for a graph callback.
    String _make_function(const String &, const String &, const PackedStringArray &) const override { return {}; }
    /// Report whether a method can be located on a source line.
    int32_t _find_function(const String &, const String &) const override { return -1; }
    /// Return no reserved textual words for the graph editor.
    PackedStringArray _get_reserved_words() const override { return {}; }
    /// Report whether a textual word changes graph flow.
    bool _is_control_flow_keyword(const String &) const override { return false; }
    /// Describe the optional Godot variant-text comment delimiter.
    PackedStringArray _get_comment_delimiters() const override {
        PackedStringArray result;
        result.append(";");
        return result;
    }
    /// Return the textual quote delimiters recognized by the asset format.
    PackedStringArray _get_string_delimiters() const override {
        PackedStringArray result;
        result.append("\" \"");
        return result;
    }
    /// Return the list of built-in graph templates.
    TypedArray<Dictionary> _get_built_in_templates(const StringName &) const override { return {}; }
    /// Validate a script source when Godot requests an editor check.
    Dictionary _validate(const String &source, const String &, bool, bool, bool, bool) const override {
        Ref<VisualScript> script;
        script.instantiate();
        script->_set_source_code(source);
        Dictionary result;
        result["valid"] = script->_is_valid();
        return result;
    }
    /// Return no completion for text-based editing of serialized graphs.
    Dictionary _complete_code(const String &, const String &, Object *) const override { return {}; }
    /// Return no text lookup for graph nodes.
    Dictionary _lookup_code(const String &, const String &, const String &, Object *) const override { return {}; }
    /// Preserve the variant text if the script editor requests indentation.
    String _auto_indent_code(const String &source, int32_t, int32_t) const override { return source; }
    /// Godot globals are accessed by graph operations.
    void _add_global_constant(const StringName &, const Variant &) override {}
    /// Godot globals are accessed by graph operations.
    void _add_named_global_constant(const StringName &, const Variant &) override {}
    /// No named global needs cleanup in this adapter.
    void _remove_named_global_constant(const StringName &) override {}
    /// Graphs run on the attached scene's thread.
    void _thread_enter() override {}
    /// The language keeps no per-thread state.
    void _thread_exit() override {}
    /// Describe the current visual break without classifying it as a script error.
    String _debug_get_error() const override {
#ifdef DEBUG_ENABLED
        if (const auto *frame = paused_visual_debug_frame()) {
            return String(U"Visual Script \u00B7 ") + frame->instance + String(U" \u00B7 node ") + String::num_int64(frame->node);
        }
#endif
        return {};
    }
    /// Supply the stopped invocation to Godot's Stack Trace.
    int32_t _debug_get_stack_level_count() const override {
#ifdef DEBUG_ENABLED
        return paused_visual_debug_frame() ? 1 : 0;
#else
        return 0;
#endif
    }
    /// Send the virtual source line that identifies this graph node.
    int32_t _debug_get_stack_level_line(int32_t level) const override {
#ifdef DEBUG_ENABLED
        if (const auto *frame = paused_visual_debug_frame(level)) {
            return visual_breakpoint_line(frame->graph, frame->node);
        }
#endif
        return -1;
    }
    /// Name the paused event graph or authored callable.
    String _debug_get_stack_level_function(int32_t level) const override {
#ifdef DEBUG_ENABLED
        if (const auto *frame = paused_visual_debug_frame(level)) {
            return frame->title;
        }
#endif
        return {};
    }
    /// Return the resource path so Godot can locate the visual script.
    String _debug_get_stack_level_source(int32_t level) const override {
#ifdef DEBUG_ENABLED
        if (const auto *frame = paused_visual_debug_frame(level)) {
            return frame->script;
        }
#endif
        return {};
    }
    /// Expose resolved pins and local variables for the suspended instruction.
    Dictionary _debug_get_stack_level_locals(int32_t level, int32_t, int32_t) override {
#ifdef DEBUG_ENABLED
        if (const auto *frame = paused_visual_debug_frame(level)) {
            visual_debug_inspection_ready(level);
            Dictionary values = frame->locals.duplicate();
            for (const Variant &pin : frame->inputs.keys()) {
                values["Pin: " + String(pin)] = frame->inputs[pin];
            }
            if (!frame->input_error.is_empty()) {
                values["Input error"] = frame->input_error;
            }
            return visual_debug_values("locals", values);
        }
#endif
        return {};
    }
    /// Show persistent graph variables at the time of the break.
    Dictionary _debug_get_stack_level_members(int32_t level, int32_t, int32_t) override {
#ifdef DEBUG_ENABLED
        if (const auto *frame = paused_visual_debug_frame(level)) {
            return visual_debug_values("members", frame->members);
        }
#endif
        return {};
    }
    /// Return no active script instance to the text debugger.
    void *_debug_get_stack_level_instance(int32_t) override { return nullptr; }
    /// Return no global graph variables to the text debugger.
    Dictionary _debug_get_globals(int32_t, int32_t) override { return {}; }
    /// Return no evaluation for text expressions in the debugger.
    String _debug_parse_stack_level_expression(int32_t, const String &, int32_t, int32_t) override { return {}; }
    /// Return the active stack information supported by this language.
    TypedArray<Dictionary> _debug_get_current_stack_info() override { return {}; }
    /// Graph refresh is handled by the graph editor's reload command.
    void _reload_all_scripts() override {}
    /// Preserve open graph edits when the text editor reloads scripts.
    void _reload_scripts(const Array &, bool) override {}
    /// Validate a graph when Godot requests a script reload.
    void _reload_tool_script(const Ref<Script> &script, bool keep_state) override {
        const Ref<VisualScript> graph = script;
        if (graph.is_valid()) {
            graph->_reload(keep_state);
        }
    }
    /// Report public functions published by the text language.
    TypedArray<Dictionary> _get_public_functions() const override { return {}; }
    /// Report constants published by the text language.
    Dictionary _get_public_constants() const override { return {}; }
    /// Report annotations published by the text language.
    TypedArray<Dictionary> _get_public_annotations() const override { return {}; }
    /// Start graph profiling when supported.
    void _profiling_start() override {}
    /// Finish graph profiling when supported.
    void _profiling_stop() override {}
    /// Handle the debugger preference for native calls.
    void _profiling_set_save_native_calls(bool) override {}
    /// The event-driven runtime needs no language update on each frame.
    void _frame() override {}
};

/// Read standalone graphs as Script resources so Godot can attach or export them.
class VisualScriptLoader final : public ResourceFormatLoader {
    GDCLASS(VisualScriptLoader, ResourceFormatLoader)

protected:
    /// All registration uses Godot's ResourceFormatLoader virtual interface.
    static void _bind_methods() {}

public:
    /// Offer the graph extension in resource and script pickers.
    PackedStringArray _get_recognized_extensions() const override {
        PackedStringArray result;
        result.append(extension);
        return result;
    }
    /// Match either the native script class or a loader hint for its base class.
    bool _handles_type(const StringName &type) const override {
        return type == StringName("VisualScript") || type == StringName("ScriptExtension") ||
               type == StringName("Script") || type == StringName("Resource");
    }
    /// Allow Godot to identify graph files as attachable script assets.
    String _get_resource_type(const String &path) const override {
        return path.get_extension().nocasecmp_to(extension) == 0 ? String("VisualScript") : String();
    }
    /// List referenced pin assets so Godot can find them during selective export.
    PackedStringArray _get_dependencies(const String &path, bool add_types) const override {
        const Ref<FileAccess> file = FileAccess::open(path, FileAccess::READ);
        if (file.is_null()) {
            return {};
        }
        const Variant source = UtilityFunctions::str_to_var(file->get_as_text());
        if (source.get_type() != Variant::DICTIONARY) {
            return {};
        }
        const Variant graph = Dictionary(source).get("graph", Variant());
        return graph.get_type() == Variant::DICTIONARY ? graph_asset_dependencies(graph, add_types) : PackedStringArray();
    }
    /// Update saved pin asset paths after Godot moves or replaces project files.
    Error _rename_dependencies(const String &path, const Dictionary &renames) const override {
        Ref<FileAccess> file = FileAccess::open(path, FileAccess::READ);
        if (file.is_null()) {
            return FileAccess::get_open_error();
        }
        const Variant decoded = UtilityFunctions::str_to_var(file->get_as_text());
        file.unref();
        if (decoded.get_type() != Variant::DICTIONARY) {
            return ERR_PARSE_ERROR;
        }
        const Variant graph = Dictionary(decoded).get("graph", Variant());
        if (graph.get_type() != Variant::DICTIONARY) {
            return ERR_PARSE_ERROR;
        }
        if (!rename_graph_assets(graph, renames)) {
            return OK;
        }
        return publish_graph(path, UtilityFunctions::var_to_str(decoded) + "\n");
    }
    /// Return a graph resource or a Godot error for an unreadable file.
    Variant _load(const String &path, const String &, bool, int32_t) const override {
        const Ref<FileAccess> file = FileAccess::open(path, FileAccess::READ);
        if (file.is_null()) {
            return FileAccess::get_open_error();
        }
        Ref<VisualScript> script;
        script.instantiate();
        script->_set_source_code(file->get_as_text());
        if (!script->_is_valid()) {
            UtilityFunctions::push_error("[Crimson] Could not load Visual Script '" + path +
                                         "'. Check its format and native base class.");
            return ERR_PARSE_ERROR;
        }
        return script;
    }
};

/// Save visual scripts as readable graph files with Godot's ResourceSaver.
class VisualScriptSaver final : public ResourceFormatSaver {
    GDCLASS(VisualScriptSaver, ResourceFormatSaver)

protected:
    /// All registration uses Godot's ResourceFormatSaver virtual interface.
    static void _bind_methods() {}

public:
    /// Recognize only resources belonging to this visual language.
    bool _recognize(const Ref<Resource> &resource) const override {
        return resource.is_valid() && Object::cast_to<VisualScript>(resource.ptr()) != nullptr;
    }
    /// Offer the visual script extension in Save dialogs.
    PackedStringArray _get_recognized_extensions(const Ref<Resource> &resource) const override {
        PackedStringArray result;
        if (_recognize(resource)) {
            result.append(extension);
        }
        return result;
    }
    /// Only intercept writes to the visual script extension.
    bool _recognize_path(const Ref<Resource> &resource, const String &path) const override {
        return _recognize(resource) && path.get_extension().nocasecmp_to(extension) == 0;
    }
    /// Write the graph to disk and report file errors to the caller.
    Error _save(const Ref<Resource> &resource, const String &path, uint32_t) override {
        const Ref<VisualScript> script = resource;
        if (script.is_null() || !script->_is_valid()) {
            return ERR_INVALID_DATA;
        }
        return publish_graph(path, script->_get_source_code() + "\n");
    }
};

/// Language object owned by the module until scene deinitialization.
VisualScriptLanguage *language = nullptr;
/// Keeps the ResourceLoader registration alive.
Ref<VisualScriptLoader> loader;
/// Keeps the ResourceSaver registration alive.
Ref<VisualScriptSaver> saver;

} // namespace

void set_graph_open_callback(GraphOpenCallback callback) {
    open_graph = callback;
}

ScriptLanguage *visual_script_language() {
    return language;
}

void register_visual_scripting(ModuleInitializationLevel level) {
    if (level != MODULE_INITIALIZATION_LEVEL_SCENE) {
        return;
    }
    GDREGISTER_CLASS(VisualScript);
    GDREGISTER_INTERNAL_CLASS(VisualScriptLanguage);
    GDREGISTER_INTERNAL_CLASS(VisualScriptLoader);
    GDREGISTER_INTERNAL_CLASS(VisualScriptSaver);

    language = memnew(VisualScriptLanguage);
    const Error status = Engine::get_singleton()->register_script_language(language);
    if (status != OK) {
        memdelete(language);
        language = nullptr;
        UtilityFunctions::push_error("[Crimson] Could not register Crimson Visual Script. Restart Godot and check for a conflicting script language.");
        return;
    }
    loader.instantiate();
    saver.instantiate();
#ifdef DEBUG_ENABLED
    initialize_visual_debugger();
#endif
    ResourceLoader::get_singleton()->add_resource_format_loader(loader, true);
    ResourceSaver::get_singleton()->add_resource_format_saver(saver, true);
}

void unregister_visual_scripting(ModuleInitializationLevel level) {
    if (level != MODULE_INITIALIZATION_LEVEL_SCENE || !language) {
        return;
    }
#ifdef DEBUG_ENABLED
    shutdown_visual_debugger();
#endif
    ResourceLoader::get_singleton()->remove_resource_format_loader(loader);
    ResourceSaver::get_singleton()->remove_resource_format_saver(saver);
    loader.unref();
    saver.unref();
    Engine::get_singleton()->unregister_script_language(language);
    memdelete(language);
    language = nullptr;
}

} // namespace crimson::visual_scripting
