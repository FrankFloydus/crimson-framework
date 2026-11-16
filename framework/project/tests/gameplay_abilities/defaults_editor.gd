# Copyright Francesco Di Blasi 2026 - All Rights Reserved

@tool
extends EditorPlugin

const GD_PATH: String = "res://scripted_guard.gd"
const GRAPH_PATH: String = "res://visual_dash.crimsonscript"
const GD_TAG: StringName = &"ability.training.guard"
const GRAPH_TAG: StringName = &"ability.training.dash"
const SHARED_TAG: StringName = &"state.control.stun"
const CAPTURE: String = "res://ability_defaults_editor.png"

## Saved ability scripts and the editor surface they share.
var gd_ability: Script
var visual_ability: Script
var defaults: Window
var tag_picker: Window
var tag_tree: Tree


## Wait for the editor to install its native plugins.
func _enter_tree() -> void:
	_verify.call_deferred()


## Configure, reopen, and clear defaults through the actual editor controls.
func _verify() -> void:
	await _settle()
	if not await _write_scripts():
		return
	defaults = get_tree().root.find_child("AbilityDefaultsWindow", true, false) as Window
	tag_picker = get_tree().root.find_child("GameplayTagPicker", true, false) as Window
	if not _need(defaults != null and tag_picker != null, "Install one Ability Defaults window and the shared tag picker."):
		return
	tag_tree = tag_picker.find_child("GameplayTagTree", true, false) as Tree
	for path: StringName in [GD_TAG, GRAPH_TAG, SHARED_TAG]:
		if not _need(GameplayTagCatalog.add_project_tag(path) == OK, "Declare an isolated catalog tag."):
			return
	if not (await _verify_gdscript() and await _verify_visual_script() and await _verify_clear() and
			await _verify_replaced_identity()):
		return
	print("CRIMSON_ABILITY_DEFAULTS_EDITOR_PASSED")
	get_tree().quit()


## Save standalone ability definitions in both languages and wait for Godot's UIDs.
func _write_scripts() -> bool:
	var source: FileAccess = FileAccess.open(GD_PATH, FileAccess.WRITE)
	if not _need(source != null, "Open the GDScript fixture path."):
		return false
	source.store_string("extends GameplayAbility\n")
	source.close()
	var visual: Script = ClassDB.instantiate(&"VisualScript") as Script
	if not _need(visual != null, "Create the Visual Script fixture."):
		return false
	visual.call(&"set_graph_base", &"GameplayAbility")
	if not _need(ResourceSaver.save(visual, GRAPH_PATH) == OK, "Save the ability graph as a standalone script."):
		return false
	var filesystem: EditorFileSystem = EditorInterface.get_resource_filesystem()
	while filesystem.is_scanning():
		await get_tree().process_frame
	filesystem.scan()
	for frame: int in range(300):
		await get_tree().process_frame
		if ResourceLoader.get_resource_uid(GD_PATH) >= 0 and ResourceLoader.get_resource_uid(GRAPH_PATH) >= 0:
			break
	if not _need(ResourceLoader.get_resource_uid(GD_PATH) >= 0 and ResourceLoader.get_resource_uid(GRAPH_PATH) >= 0,
			"Both Godot script loaders assign a persistent UID."):
		return false
	gd_ability = ResourceLoader.load(GD_PATH, "Script") as Script
	visual_ability = ResourceLoader.load(GRAPH_PATH, "Script") as Script
	return _need(gd_ability != null and visual_ability != null, "Load the saved abilities without any lifecycle code yet.")


## Use the Inspector entry point and verify nested picker Cancel restores focus.
func _verify_gdscript() -> bool:
	var inspector_action: Button = await _open_inspected(gd_ability)
	if not _need(inspector_action != null and _shows_script(GD_PATH), "Open the GDScript defaults from the Inspector."):
		return false
	var field: Button = defaults.find_child("AbilityDefaultsTags_ability_tags", true, false) as Button
	if not _need(field != null and await _pick_tag("ability_tags", GD_TAG, false, true),
			"Open and cancel the shared gameplay tag picker."):
		return false
	if not _need(field.has_focus() and defaults.visible and not _has_tag("ability_tags", GD_TAG),
			"Cancel closes only the picker, restores field focus, and preserves the draft."):
		return false
	if not await _pick_tag("ability_tags", GD_TAG):
		return false
	if not _press("AbilityDefaultsSave", defaults):
		return false
	await _settle()
	return _need(not defaults.visible and inspector_action.has_focus(), "Save closes Ability Defaults and restores Inspector focus.")


## Open the same window from the Visual Script toolbar and edit independent defaults.
func _verify_visual_script() -> bool:
	EditorInterface.edit_script(visual_ability, -1, 0, true)
	await _settle()
	var workspace: Control = get_tree().root.find_child("CrimsonVisualScriptWorkspace", true, false) as Control
	if not _need(workspace != null and _press("GraphAbilityDefaults", workspace), "Use the saved ability graph's toolbar button."):
		return false
	await _settle()
	if not _need(_shows_script(GRAPH_PATH) and not _has_tag("ability_tags", GD_TAG),
			"Open the same form with the Visual Script's own defaults."):
		return false
	if not await _pick_tag("ability_tags", GRAPH_TAG) or not await _pick_tag("activation_blocked_tags", SHARED_TAG):
		return false
	if "--capture-ability-defaults-ui" in OS.get_cmdline_user_args():
		RenderingServer.force_draw()
		var screenshot: Image = defaults.get_texture().get_image()
		if not _need(screenshot != null and not screenshot.is_empty() and screenshot.save_png(CAPTURE) == OK,
				"Capture the rendered Ability Defaults form."):
			return false
	if not _press("AbilityDefaultsSave", defaults):
		return false
	await _settle()
	var inspector_action: Button = await _open_inspected(visual_ability)
	if not _need(inspector_action != null and _has_tag("ability_tags", GRAPH_TAG) and
			_has_tag("activation_blocked_tags", SHARED_TAG),
			"Reopen the Visual Script defaults from the Inspector and recover both saved fields."):
		return false
	return _press("AbilityDefaultsCancel", defaults)


## Clear a previous choice and make sure the change survives reopening.
func _verify_clear() -> bool:
	var inspector_action: Button = await _open_inspected(gd_ability)
	if not _need(inspector_action != null and _has_tag("ability_tags", GD_TAG),
			"The GDScript choice stays independent from the Visual Script."):
		return false
	var chips: Control = defaults.find_child("AbilityDefaultsChips_ability_tags", true, false) as Control
	if not _press("RemoveAbilityDefaultTag", chips):
		return false
	if not _press("AbilityDefaultsCancel", defaults):
		return false
	inspector_action = await _open_inspected(gd_ability)
	if not _need(inspector_action != null and _has_tag("ability_tags", GD_TAG), "Cancel discards the pending removal."):
		return false
	if not _press("RemoveAbilityDefaultTag", chips) or not _press("AbilityDefaultsSave", defaults):
		return false
	inspector_action = await _open_inspected(gd_ability)
	if not _need(inspector_action != null and not _has_tag("ability_tags", GD_TAG), "Save persists an empty field."):
		return false
	var file_contents: String = FileAccess.get_file_as_string("res://project.godot")
	return (_need(String(GRAPH_TAG) in file_contents, "The other script's saved defaults remain in the project file.") and
			_press("AbilityDefaultsCancel", defaults))


## Replace the script's UID while a draft is open and reject the stale save.
func _verify_replaced_identity() -> bool:
	var inspector_action: Button = await _open_inspected(gd_ability)
	if not _need(inspector_action != null and await _pick_tag("ability_tags", GD_TAG), "Prepare a draft for the original script."):
		return false
	var original_settings: String = FileAccess.get_file_as_string("res://project.godot")
	var sidecar_path: String = GD_PATH + ".uid"
	var original_uid: String = FileAccess.get_file_as_string(sidecar_path)
	var replacement: FileAccess = FileAccess.open(sidecar_path, FileAccess.WRITE)
	if not _need(replacement != null and not original_uid.is_empty(), "Replace the physical sidecar while the draft is open."):
		return false
	replacement.store_line(ResourceUID.id_to_text(ResourceUID.create_id()))
	replacement.close()
	var attempted_save: bool = _press("AbilityDefaultsSave", defaults)
	var restore: FileAccess = FileAccess.open(sidecar_path, FileAccess.WRITE)
	if not _need(restore != null, "Restore the fixture's original resource identity."):
		return false
	restore.store_string(original_uid)
	restore.close()
	var status: Label = defaults.find_child("AbilityDefaultsStatus", true, false) as Label
	return (_need(attempted_save and defaults.visible and "reopen Ability Defaults" in status.text and
			FileAccess.get_file_as_string("res://project.godot") == original_settings,
			"A replacement UID keeps the original draft open and does not change the project file.") and
			_press("AbilityDefaultsCancel", defaults))


## Launch the shared form from Godot's selected Script resource.
func _open_inspected(script_resource: Script) -> Button:
	EditorInterface.inspect_object(script_resource)
	await _settle()
	var inspector: EditorInspector = EditorInterface.get_inspector()
	var action: Button = inspector.find_child("InspectorAbilityDefaults", true, false) as Button
	if not _press("InspectorAbilityDefaults", inspector):
		return null
	await _settle()
	return action


## Choose one exact tag through the ordinary button and keyboard picker flow.
func _pick_tag(field: String, path: StringName, apply: bool = true, use_pointer: bool = false) -> bool:
	var action_name: String = "AbilityDefaultsTags_" + field
	if use_pointer:
		var target: Button = defaults.find_child(action_name, true, false) as Button
		await _pointer_press(target)
	elif not _press(action_name, defaults):
		return false
	await _settle()
	var item: TreeItem = _tag_item(tag_tree.get_root(), path)
	if not _need(tag_picker.visible and item != null, "Open the shared tag catalog for " + field + "."):
		return false
	item.select(0)
	tag_tree.item_activated.emit()
	if apply:
		if not _press("GameplayTagApply", tag_picker):
			return false
	else:
		var back: InputEventKey = InputEventKey.new()
		back.keycode = KEY_ESCAPE
		back.pressed = true
		tag_picker.push_input(back, true)
	await _settle()
	return _need(not tag_picker.visible and defaults.visible and _has_tag(field, path) == apply,
			"The nested selection resolves inside the draft.")


## Find an exact path inside the catalog tree.
func _tag_item(parent: TreeItem, path: StringName) -> TreeItem:
	if parent != null:
		for child: TreeItem in parent.get_children():
			if child.get_metadata(0) == String(path):
				return child
			var nested: TreeItem = _tag_item(child, path)
			if nested != null:
				return nested
	return null


## Compare the dialog's selected script identity.
func _shows_script(path: String) -> bool:
	var file_label: Label = defaults.find_child("AbilityDefaultsScriptPath", true, false) as Label
	return defaults.visible and file_label != null and file_label.text == path


## Read the visible chips for one defaults field.
func _has_tag(field: String, path: StringName) -> bool:
	var area: Control = defaults.find_child("AbilityDefaultsChips_" + field, true, false) as Control
	if area != null:
		for chip: Node in area.find_children("*", "Label", true, false):
			var caption: Label = chip as Label
			if caption.text == String(path):
				return true
	return false


## Invoke a visible native command through its normal button action.
func _press(action_name: String, scope: Node) -> bool:
	var action: Button = scope.find_child(action_name, true, false) as Button
	if not _need(action != null and action.is_visible_in_tree() and not action.disabled, "Use " + action_name + "."):
		return false
	action.grab_focus()
	action.pressed.emit()
	return true


## Send pointer input through the window that owns the desktop coordinates.
func _pointer_press(action: Button) -> void:
	var position: Vector2 = action.get_global_rect().get_center()
	var input_window: Window = action.get_window()
	while input_window.is_embedded():
		position = input_window.get_final_transform() * position + Vector2(input_window.position)
		input_window = input_window.get_parent().get_window()
	var native_pointer: bool = DisplayServer.get_name() != "headless"
	var previous_pointer: Vector2i = DisplayServer.mouse_get_position()
	if native_pointer:
		input_window.warp_mouse(position)
		await get_tree().process_frame
	for down: bool in [true, false]:
		var click: InputEventMouseButton = InputEventMouseButton.new()
		click.position = position
		click.global_position = position
		click.button_index = MOUSE_BUTTON_LEFT
		click.button_mask = MOUSE_BUTTON_MASK_LEFT if down else 0
		click.pressed = down
		click.window_id = input_window.get_window_id()
		input_window.push_input(click, true)
	if native_pointer:
		DisplayServer.warp_mouse(previous_pointer)


## Allow deferred popup and Inspector layout to finish.
func _settle() -> void:
	for frame: int in range(8):
		await get_tree().process_frame


## Report the failing editor action to the harness.
func _need(condition: bool, description: String) -> bool:
	if not condition:
		push_error("Ability Defaults editor fixture. " + description)
		get_tree().quit(1)
	return condition
