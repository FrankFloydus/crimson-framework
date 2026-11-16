# Copyright Francesco Di Blasi 2026 - All Rights Reserved

@tool
extends EditorPlugin

const GRAPH_PATH: String = "res://ability_editor_fixture.crimsonscript"
const PICKER_POINT: Vector2 = Vector2(120, 160)
const CAPTURE: String = "res://gameplay_ability_callback_picker.png"

## The isolated ability graph and its editor controls.
var authored_script: Script
var canvas: GraphEdit
var palette: PopupPanel
var search: LineEdit
var results: Tree


## Start once the extension has installed its editor workspace.
func _enter_tree() -> void:
	_verify.call_deferred()


## Create the callback declarations using the same palette as a scene author.
func _verify() -> void:
	await _settle()
	while EditorInterface.get_resource_filesystem().is_scanning():
		await get_tree().process_frame
	var draft: Script = ClassDB.instantiate(&"VisualScript") as Script
	if not _need(draft != null, "Create a Visual Script resource."):
		return
	draft.call(&"set_graph_base", &"GameplayAbility")
	if not _need(ResourceSaver.save(draft, GRAPH_PATH) == OK, "Save an isolated ability graph."):
		return
	authored_script = ResourceLoader.load(GRAPH_PATH, "Script") as Script
	EditorInterface.edit_script(authored_script, -1, 0, true)
	await _settle()
	var workspace: Control = get_tree().root.find_child("CrimsonVisualScriptWorkspace", true, false) as Control
	if not _need(workspace != null and workspace.is_visible_in_tree(), "Open the Visual Script workspace."):
		return
	canvas = workspace.find_child("GraphCanvas", true, false) as GraphEdit
	palette = workspace.find_child("GraphNodePalette", true, false) as PopupPanel
	search = workspace.find_child("GraphPaletteSearch", true, false) as LineEdit
	results = workspace.find_child("GraphPaletteResults", true, false) as Tree
	if not _need(canvas != null and palette != null and search != null and results != null, "Find the native graph palette."):
		return
	if not await _verify_events() or not await _verify_native_methods() or not await _verify_guard(workspace):
		return
	print("CRIMSON_VISUAL_SCRIPT_GAMEPLAY_ABILITY_EDITOR_PASSED")
	get_tree().quit()


## Author typed Event Graph entries and suppress options for existing callbacks.
func _verify_events() -> bool:
	canvas.popup_request.emit(PICKER_POINT)
	_search("Gameplay Abilities")
	await _settle()
	var group: TreeItem = _child_named(results.get_root(), "Gameplay Abilities")
	if not _need(group != null and _child_named(group, "On Activated") != null and
			_child_named(group, "On Canceled") != null and _child_named(group, "On Ended") != null and
			_child_named(group, "Can Activate") != null, "Group the four ability callbacks in the Event Graph palette."):
		return false
	if "--capture-gameplay-ability-editor" in OS.get_cmdline_user_args():
		RenderingServer.force_draw()
		var screenshot: Image = palette.get_texture().get_image()
		if not _need(screenshot != null and not screenshot.is_empty() and screenshot.save_png(CAPTURE) == OK,
				"Capture the rendered callback picker."):
			return false
	_select(_child_named(group, "On Activated"))
	await _settle()
	canvas.popup_request.emit(PICKER_POINT + Vector2(330, 0))
	_search("Gameplay Abilities")
	group = _child_named(results.get_root(), "Gameplay Abilities")
	var ended: TreeItem = _child_named(group, "On Ended")
	if not _need(group != null and _child_named(group, "On Activated") == null and ended != null,
			"An existing callback stays intact and no duplicate action is offered."):
		return false
	_select(ended)
	await _settle()
	canvas.popup_request.emit(PICKER_POINT + Vector2(0, 180))
	_search("Gameplay Abilities")
	group = _child_named(results.get_root(), "Gameplay Abilities")
	var canceled: TreeItem = _child_named(group, "On Canceled")
	if not _need(canceled != null and _child_named(group, "On Ended") == null,
			"Offer On Canceled separately from On Ended."):
		return false
	_select(canceled)
	await _settle()

	var document: Dictionary = authored_script.call(&"get_graph")
	var on_activated: Dictionary = _declared(document, &"events", "on_activated")
	var on_canceled: Dictionary = _declared(document, &"events", "on_canceled")
	var on_ended: Dictionary = _declared(document, &"events", "on_ended")
	if not _need(_typed_activation(on_activated, 1) and _typed_activation(on_canceled, 1) and
			_typed_activation(on_ended, 2), "Each event exposes the typed activation context."):
		return false
	var canceled_card: GraphNode = _event_card(document, on_canceled.id)
	if not _need(canceled_card != null and _output_port(canceled_card, &"activation") >= 0,
			"On Canceled renders its activation output in the Event Graph."):
		return false
	var canceled_pin: Dictionary = on_ended.inputs[1]
	return _need(canceled_pin.get("id") == &"canceled" and canceled_pin.get("type") == TYPE_BOOL,
			"On Ended receives a typed Canceled pin.")


## Follow a typed Activation result through the existing native method picker.
func _verify_native_methods() -> bool:
	var document: Dictionary = authored_script.call(&"get_graph")
	var on_activated: Dictionary = _declared(document, &"events", "on_activated")
	var event_card: GraphNode = _event_card(document, on_activated.id)
	if not _need(event_card != null, "The activation event renders on the Event Graph."):
		return false
	var activation_port: int = _output_port(event_card, &"activation")
	if not _need(activation_port >= 0, "The event renders the activation data socket."):
		return false
	canvas.connection_to_empty.emit(event_card.name, activation_port, PICKER_POINT)
	if not _find_method(&"GameplayAbilityActivation", &"get_component"):
		return false
	_select(results.get_selected())
	await _settle()
	document = authored_script.call(&"get_graph")
	var component_card: GraphNode
	for node: Dictionary in document.nodes:
		var method: Dictionary = node.get("member", {})
		if method.get("name") == &"get_component":
			component_card = _card(node.id)
			break
	if not _need(component_card != null, "Insert Get Component from the activation data socket."):
		return false
	var component_port: int = _output_port(component_card, &"result")
	if not _need(component_port >= 0, "Get Component renders a typed result socket."):
		return false
	canvas.connection_to_empty.emit(component_card.name, component_port, PICKER_POINT)
	var found: bool = (_find_method(&"AbilitySystemComponent", &"give_ability") and
			_find_method(&"AbilitySystemComponent", &"try_activate_ability"))
	palette.hide()
	return found


## Create the synchronous bool guard with a default path that allows activation.
func _verify_guard(workspace: Control) -> bool:
	canvas.popup_request.emit(PICKER_POINT)
	_search("Gameplay Abilities")
	var group: TreeItem = _child_named(results.get_root(), "Gameplay Abilities")
	var choice: TreeItem = _child_named(group, "Can Activate")
	if not _need(choice != null and _child_named(group, "On Canceled") == null,
			"Can Activate remains available and the created On Canceled is not offered again."):
		return false
	_select(choice)
	await _settle()
	var document: Dictionary = authored_script.call(&"get_graph")
	var declaration: Dictionary = _declared(document, &"functions", "on_can_activate")
	if not _need(_typed_activation(declaration, 1) and declaration.outputs.size() == 1,
			"Can Activate creates a function taking the activation context and returning one value."):
		return false
	var allowed: Dictionary = declaration.outputs[0]
	var function_graph: Dictionary = declaration.graph
	var return_node: Dictionary = function_graph.nodes[1]
	var return_values: Dictionary = return_node.get("values", {})
	var caption: Label = workspace.find_child("VisualScriptActiveGraph", true, false) as Label
	return _need(allowed.get("id") == &"allowed" and allowed.get("type") == TYPE_BOOL and
			return_values.get("allowed") == true and function_graph.connections.size() == 1 and
			caption != null and caption.is_visible_in_tree(),
			"Open the function and connect its entry to a bool return initialized to true.")


## Find one declared hook in the saved graph document.
func _declared(document: Dictionary, section: StringName, callback: String) -> Dictionary:
	var declarations: Array = document.get(section, [])
	for declaration: Dictionary in declarations:
		if declaration.get("name") == callback:
			return declaration
	return {}


## Compare the first typed parameter and exact argument count.
func _typed_activation(declaration: Dictionary, expected_count: int) -> bool:
	var inputs: Array = declaration.get("inputs", [])
	if inputs.size() != expected_count:
		return false
	var context: Dictionary = inputs[0]
	return (context.get("id") == &"activation" and context.get("type") == TYPE_OBJECT and
			context.get("class") == &"GameplayAbilityActivation")


## Inspect a reflected method returned by searching from a typed socket.
func _find_method(native_owner: StringName, method_name: StringName) -> bool:
	var qualified: String = String(native_owner) + "." + String(method_name)
	_search(qualified)
	var selected: TreeItem = results.get_selected()
	var option: Dictionary = selected.get_metadata(0) if selected != null else {}
	var method: Dictionary = option.get("member", {})
	return _need(method.get("class") == native_owner and method.get("name") == method_name,
			"Find native method " + qualified + ".")


## Resolve one canvas card by its stable authored ID.
func _card(id: int) -> GraphNode:
	return canvas.get_node_or_null("GraphNode_%d" % id) as GraphNode


## Find the Event Graph entry linked to a named event declaration.
func _event_card(document: Dictionary, declaration_id: int) -> GraphNode:
	for node: Dictionary in document.nodes:
		if node.get("kind") == "custom_event" and node.get("definition") == declaration_id:
			return _card(node.id)
	return null


## Find the outgoing port with the saved pin identity.
func _output_port(card: GraphNode, id: StringName) -> int:
	for index: int in range(card.get_output_port_count()):
		var descriptor: Dictionary = card.get_slot_metadata_right(card.get_output_port_slot(index))
		if descriptor.get("id") == id:
			return index
	return -1


## Filter the native palette as a keyboard user.
func _search(query: String) -> void:
	search.text = query
	search.text_changed.emit(query)


## Submit the chosen item using the palette's normal keyboard action.
func _select(item: TreeItem) -> void:
	item.select(0)
	search.text_submitted.emit(search.text)


## Read a named immediate child in the native palette.
func _child_named(parent: TreeItem, title: String) -> TreeItem:
	if parent != null:
		for item: TreeItem in parent.get_children():
			if item.get_text(0) == title:
				return item
	return null


## Allow deferred scene and graph layout to finish.
func _settle() -> void:
	for frame: int in range(6):
		await get_tree().process_frame


## Send a failed editor assertion to the isolated harness.
func _need(condition: bool, description: String) -> bool:
	if not condition:
		push_error("Gameplay Ability editor fixture. " + description)
		get_tree().quit(1)
	return condition
