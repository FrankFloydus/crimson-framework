// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#pragma once

#include <godot_cpp/classes/script_extension.hpp>
#include <godot_cpp/classes/script_language.hpp>
#include <godot_cpp/variant/dictionary.hpp>

#include <memory>
#include <map>

namespace crimson::visual_scripting {

struct CustomScriptCache;
struct VisualScriptExports;

/// An attachable Godot script containing a saved visual graph.
class VisualScript final : public godot::ScriptExtension {
    GDCLASS(VisualScript, godot::ScriptExtension)

    /// The native Godot node class accepted by this script.
    godot::StringName graph_base{"Node"};
    /// Authored graph data, shared by all instances of this script.
    godot::Dictionary graph;
    /// Whether the last loaded source could be read.
    bool source_valid = true;
    /// Generated GDScript programs shared by owners of this graph revision.
    std::shared_ptr<CustomScriptCache> custom_scripts;
    /// Inspector metadata and defaults for the current authored declarations.
    std::shared_ptr<const VisualScriptExports> exports;
    /// Engine-owned edit-time instances, removed by the placeholder destruction callback.
    mutable std::map<void *, godot::StringName> placeholders;

    /// Rebuild the published export schema when its authored declarations change.
    void refresh_exports();

protected:
    /// Register the graph snapshots used by editor transactions.
    static void _bind_methods();

public:
    /// Create an empty graph that can attach to any Node.
    VisualScript();

    /// Select the native node class this script can extend.
    void set_graph_base(const godot::StringName &base);
    /// Return the native node class this script extends.
    godot::StringName get_graph_base() const;
    /// Replace the graph with a detached copy of an editor snapshot.
    void set_graph(const godot::Dictionary &value);
    /// Read a detached snapshot without mutating the script.
    godot::Dictionary get_graph() const;
    /// Check that this name and type can be exported on all currently attached edit-time owners.
    bool can_export_for_owners(const godot::StringName &name, const godot::Dictionary &pin) const;

    /// Keep automatic editor reload from discarding graph edits.
    bool _editor_can_reload_from_file() override;
    /// Instantiate scripts that extend a native Node.
    bool _can_instantiate() const override;
    /// Report that the script extends a native class directly.
    godot::Ref<godot::Script> _get_base_script() const override;
    /// Leave generated script class names empty.
    godot::StringName _get_global_name() const override;
    /// Report script inheritance for an identical script resource.
    bool _inherits_script(const godot::Ref<godot::Script> &script) const override;
    /// Identify the owner class for Godot's attachment check.
    godot::StringName _get_instance_base_type() const override;
    /// Create one execution instance for a compatible runtime node.
    void *_instance_create(godot::Object *owner) const override;
    /// Represent the script in the editor without running its graph.
    void *_placeholder_instance_create(godot::Object *owner) const override;
    /// Forget an editor placeholder before Godot releases it.
    void _placeholder_erased(void *placeholder) override;
    /// Indicate that the graph has a text representation.
    bool _has_source_code() const override;
    /// Serialize the graph and owner class as Godot variant text.
    godot::String _get_source_code() const override;
    /// Load a graph and owner class from their text representation.
    void _set_source_code(const godot::String &code) override;
    /// Report whether the current graph source and base class are readable.
    godot::Error _reload(bool keep_state) override;
    /// Report no generated class documentation.
    godot::StringName _get_doc_class_name() const override;
    /// Report no generated member documentation.
    godot::TypedArray<godot::Dictionary> _get_documentation() const override;
    /// Report whether Godot may call a named graph method.
    bool _has_method(const godot::StringName &method) const override;
    /// Report no static graph methods.
    bool _has_static_method(const godot::StringName &method) const override;
    /// Describe a lifecycle callback or authored callable.
    godot::Dictionary _get_method_info(const godot::StringName &method) const override;
    /// Run the graph during play.
    bool _is_tool() const override;
    /// Validate the source header and native base.
    bool _is_valid() const override;
    /// Return the registered language for this script.
    godot::ScriptLanguage *_get_language() const override;
    /// Report no declared graph signals.
    bool _has_script_signal(const godot::StringName &signal) const override;
    /// Return the list of declared graph signals.
    godot::TypedArray<godot::Dictionary> _get_script_signal_list() const override;
    /// Identify an exported property, including one whose default is null.
    bool _has_property_default_value(const godot::StringName &property) const override;
    /// Return the authored default, detached from mutable instance values.
    godot::Variant _get_property_default_value(const godot::StringName &property) const override;
    /// Refresh exports when graph property definitions become available.
    void _update_exports() override;
    /// Return lifecycle and authored methods declared by this graph.
    godot::TypedArray<godot::Dictionary> _get_script_method_list() const override;
    /// Publish exported graph declarations as Godot Inspector properties.
    godot::TypedArray<godot::Dictionary> _get_script_property_list() const override;
    /// Report no source line for graph members.
    int32_t _get_member_line(const godot::StringName &member) const override;
    /// Return graph constants exposed to Godot.
    godot::Dictionary _get_constants() const override;
    /// Return graph fields exposed to Godot.
    godot::TypedArray<godot::StringName> _get_members() const override;
    /// Keep editor placeholders limited to the script schema.
    bool _is_placeholder_fallback_enabled() const override;
    /// Report no graph RPC configuration.
    godot::Variant _get_rpc_config() const override;
    /// Compare the owner's attached resource with this script.
    bool _instance_has(godot::Object *owner) const override;
};

/// Check if an instance variable can be exposed on the specified native owner class.
bool can_export_variable(const godot::StringName &base, const godot::StringName &name, const godot::Dictionary &pin);

/// Return whether the pin supports a numeric range and units in Godot's Inspector.
bool has_numeric_inspector_options(const godot::Dictionary &pin);

/// Check the numeric Inspector limits and step before publishing or saving a hint.
bool is_valid_inspector_range(const godot::Dictionary &pin, const godot::Dictionary &range);

/// Check whether a unit label can be stored in Godot's comma-separated hint string.
bool is_valid_inspector_suffix(const godot::String &suffix);

/// Check whether a label can be displayed as one Inspector group.
bool is_valid_inspector_group(const godot::String &group);

/// Editor hook that displays an attached script in the graph workspace.
using GraphOpenCallback = void (*)(const godot::Ref<VisualScript> &script);

/// Install or clear the editor's graph opening hook.
void set_graph_open_callback(GraphOpenCallback callback);

} // namespace crimson::visual_scripting
