// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#pragma once

#ifdef DEBUG_ENABLED

#include <godot_cpp/classes/control.hpp>
#include <godot_cpp/classes/resource.hpp>
#include <functional>

namespace crimson::visual_scripting {

/// Read a single saved project file from a Godot FileSystem drag payload.
godot::String dropped_project_file(const godot::Variant &data);

/// Open a project resource file dialog and pass the chosen res:// path to the caller.
void browse_project_resource(godot::Node *owner, const godot::StringName &type, const godot::Callable &chosen);

/// Add a typed project resource chooser used by graph pins and declaration defaults.
godot::Control *asset_field(godot::Node *parent, const godot::StringName &type, const godot::String &help,
        const godot::Variant &stored, const std::function<bool(const godot::Ref<godot::Resource> &)> &change,
        bool inline_graph = false, bool preload = false);

/// Display a saved resource's editor thumbnail or class icon without evaluating the graph.
void add_preload_preview(godot::Node *parent, const godot::Variant &stored, const godot::StringName &asset_class);

/// Register the editor-only resource control.
void register_visual_script_asset_field();

} // namespace crimson::visual_scripting

#endif
