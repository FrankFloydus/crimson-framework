# Copyright Francesco Di Blasi 2026 - All Rights Reserved

@tool
extends EditorPlugin

const SETTING: String = "crimson/gameplay_attributes/sets"
const SETTINGS_FILE: String = "res://project.godot"
const TAB_TITLE: String = "Gameplay Attributes"
const GENERATED_HEADER: String = "res://generated/gameplay_attributes/vitals_attributes.gen.hpp"
const CAPTURE: String = "res://gameplay_attributes_editor.png"
const COMPONENT_CAPTURE: String = "res://ability_system_component_editor.png"
const COMPONENT_SCENE: String = "res://player_attributes.tscn"
const SET_ID: String = "vitals"
const ATTRIBUTE_ID: String = "health"
const DISPLAY_LABEL: String = "Hit Points"

## Native tab attached by the extension to the stock Project Settings window.
var attributes_tab: Control
## Tabs owned by Godot's Project Settings dialog.
var settings_tabs: TabContainer


## Start once the editor and the extension have installed their plugins.
func _enter_tree() -> void:
	_verify.call_deferred()


## Exercise authoring and generation in a fresh project.
func _verify() -> void:
	await _frames()
	var candidates: Array[Node] = get_tree().root.find_children("*", "GameplayAttributesPanel", true, false)
	if not _require(candidates.size() == 1, "One native Gameplay Attributes tab was installed."):
		return
	attributes_tab = candidates.front() as Control
	settings_tabs = attributes_tab.get_parent() as TabContainer
	if not _require(settings_tabs != null, "The tab is attached to Project Settings' native TabContainer."):
		return
	var settings_window: Window = attributes_tab.get_window()
	if not (await _open_settings() and await _verify_definition() and await _verify_generation() and
			await _verify_symbol_collision() and await _verify_visibility() and await _verify_legacy_storage() and
			await _verify_draft_actions()):
		return
	if not _capture(settings_window, CAPTURE) or not await _verify_component():
		return
	print("CRIMSON_GAMEPLAY_ATTRIBUTES_EDITOR_PASSED")
	get_tree().quit()


## Open Project Settings at Godot's default dimensions and use its top-level authoring tab.
func _open_settings() -> bool:
	var window: Window = attributes_tab.get_window()
	window.popup_centered_clamped(Vector2i(Vector2(1200, 700) * EditorInterface.get_editor_scale()), 0.8)
	await _frames()
	if not await _verify_general_settings():
		return false
	var index: int = settings_tabs.get_tab_idx_from_control(attributes_tab)
	if not _require(index >= 0 and settings_tabs.get_tab_title(index) == TAB_TITLE and
			not settings_tabs.is_tab_hidden(index) and not settings_tabs.is_tab_disabled(index),
			"The native tab strip exposes Gameplay Attributes as a selectable top-level tab."):
		return false
	settings_tabs.current_tab = index
	await _frames()
	return _require(attributes_tab.is_visible_in_tree(), "The dedicated authoring controls are visible.")


## Confirm that Godot's built-in filter finds settings while skipping Crimson's stored registry.
func _verify_general_settings() -> bool:
	var stored_registry: bool = false
	for property: Dictionary in ProjectSettings.get_property_list():
		if property.name == SETTING:
			var usage: int = property.usage
			stored_registry = (property.type == TYPE_DICTIONARY and (usage & PROPERTY_USAGE_STORAGE) != 0 and
					(usage & PROPERTY_USAGE_EDITOR) == 0)
			break
	if not _require(stored_registry, "The registry is stored without exposing a generic editor property."):
		return false
	settings_tabs.current_tab = 0
	await _frames()
	var general: Control = settings_tabs.get_tab_control(0)
	var filters: Array[Node] = general.find_children("*", "LineEdit", true, false)
	var trees: Array[Node] = general.find_children("*", "Tree", true, false)
	if not _require(not filters.is_empty() and not trees.is_empty(), "General has a settings filter and category tree."):
		return false
	var search: LineEdit = filters.front() as LineEdit
	var categories: Tree = trees.front() as Tree
	search.text = "application/config/name"
	search.text_changed.emit(search.text)
	await _frames()
	if not _require(categories.get_root() != null and categories.get_root().get_first_child() != null,
			"General search finds the stock project name setting."):
		return false
	search.text = SETTING
	search.text_changed.emit(search.text)
	await _frames()
	var concealed: bool = categories.get_root() == null or categories.get_root().get_first_child() == null
	search.clear()
	search.text_changed.emit(search.text)
	return _require(concealed, "General search leaves the raw Gameplay Attributes registry hidden.")


## Create stable IDs and save a definition with blank labels and default authority visibility.
func _verify_definition() -> bool:
	if not _require(_saved_sets().is_empty(), "The fixture starts with an empty registry."):
		return false
	await _add("GameplayAttributesAddSet", SET_ID)
	var set_id: LineEdit = _line("GameplayAttributesSetId")
	var add_attribute: Button = attributes_tab.find_child("GameplayAttributesAddAttribute", true, false) as Button
	if not _require(set_id.text == SET_ID and not set_id.editable and add_attribute.has_focus(),
			"The selected set has a read-only ID and focuses the next authoring action."):
		return false
	var set_list: ItemList = attributes_tab.find_child("GameplayAttributesSets", true, false) as ItemList
	_press("GameplayAttributesAddSet")
	_submit_id(SET_ID)
	if not _require(_prompt().is_visible_in_tree() and set_list.item_count == 1,
			"Duplicate set IDs keep the prompt open and preserve the draft."):
		return false
	_press("CancelDialog", _prompt())

	await _add("GameplayAttributesAddAttribute", ATTRIBUTE_ID)
	var id_field: LineEdit = _line("GameplayAttributesId")
	var label_field: LineEdit = _line("GameplayAttributesLabel")
	if not _require(id_field.text == ATTRIBUTE_ID and not id_field.editable and label_field.text.is_empty() and
			label_field.has_focus(), "The new attribute displays a read-only ID and focuses its optional label."):
		return false
	for old_field: String in ["GameplayAttributesInitial", "GameplayAttributesMinimum", "GameplayAttributesMaximum"]:
		if not _require(_line(old_field) == null, "Gameplay Attributes contains identity and replication fields."):
			return false
	_press("GameplayAttributesSave")
	return _require(_saved_attribute() == {"id": ATTRIBUTE_ID, "visibility": "authority"},
			"Save writes the stable ID and replication policy with no label requirement.")


## Generate from the same settings and verify presentation-only edits keep the output stable.
func _verify_generation() -> bool:
	_press("GameplayAttributesGenerate")
	if not _require(FileAccess.file_exists(GENERATED_HEADER), "Generate wrote the C++ header for the selected set."):
		return false
	var header: String = FileAccess.get_file_as_string(GENERATED_HEADER)
	if not _require("vitals.health" in header and "get_health(" in header and "set_health_base(" in header and
			"bind_health_changed(" in header, "Generated getters, setters, and change subscription share the qualified ID."):
		return false
	_edit_field("GameplayAttributesLabel", DISPLAY_LABEL)
	_press("GameplayAttributesGenerate")
	return _require(_saved_attribute().get("label") == DISPLAY_LABEL and
			FileAccess.get_file_as_string(GENERATED_HEADER) == header,
			"Generate saves the optional label and leaves generated C++ identity stable.")


## Reject a pair of distinct IDs that would generate the same accessor.
func _verify_symbol_collision() -> bool:
	var settings_text: String = FileAccess.get_file_as_string(SETTINGS_FILE)
	var header_text: String = FileAccess.get_file_as_string(GENERATED_HEADER)
	var status: Label = attributes_tab.find_child("GameplayAttributesStatus", true, false) as Label
	await _add("GameplayAttributesAddAttribute", "health_base")
	for editor_action: String in ["GameplayAttributesSave", "GameplayAttributesGenerate"]:
		_press(editor_action)
		if not _require("get_health_base" in status.text and "'vitals.health'" in status.text and
				"'vitals.health_base'" in status.text and
				_saved_attribute().get("id") == ATTRIBUTE_ID and
				FileAccess.get_file_as_string(SETTINGS_FILE) == settings_text and
				FileAccess.get_file_as_string(GENERATED_HEADER) == header_text,
				"Symbol collision names both attributes and preserves settings and headers for " + editor_action):
			return false
	_press("GameplayAttributesDiscard")
	_press("ConfirmDialog", _prompt())
	return _require(_line("GameplayAttributesId").text == ATTRIBUTE_ID, "Discard restores the last valid definition.")


## Choose the replicated audience using the same popup shared by Crimson editor dropdowns.
func _verify_visibility() -> bool:
	var replication: Control = attributes_tab.find_child("GameplayAttributesVisibility", true, false) as Control
	var trigger: Button = replication.find_child("SelectTrigger", true, false) as Button
	trigger.grab_focus()
	_press("SelectTrigger", replication)
	await _frames()
	var picker: PopupPanel = replication.find_child("SelectPopup", true, false) as PopupPanel
	if not _require(picker != null and picker.visible and picker.gui_get_focus_owner() is Button,
			"The replication popup opens and gives keyboard focus to a choice."):
		return false
	for child: Node in picker.find_children("*", "Button", true, false):
		var choice: Button = child as Button
		if choice.text == "Owner":
			choice.pressed.emit()
			break
	await _frames()
	if not _require(not picker.visible and trigger.has_focus() and trigger.text == "Owner",
			"Choosing Owner closes the popup and restores focus to the trigger."):
		return false
	_press("GameplayAttributesSave")
	return _require(_saved_attribute().get("visibility") == "owner", "Save persists owner-only replication.")


## Load an earlier v1 definition and remove its obsolete fields when the user saves.
func _verify_legacy_storage() -> bool:
	var expected: Dictionary = ProjectSettings.get_setting(SETTING).duplicate(true)
	var legacy: Dictionary = expected.duplicate(true)
	var attribute: Dictionary = legacy["sets"][0]["attributes"][0]
	attribute["initial"] = 100.0
	attribute["minimum"] = 0.0
	attribute["maximum"] = 150.0
	ProjectSettings.set_setting(SETTING, legacy)
	if not _require(ProjectSettings.save() == OK, "Store an existing v1 definition with numeric keys on disk."):
		return false
	attributes_tab.get_window().hide()
	await _frames()
	if not await _open_settings():
		return false
	if not _require(_line("GameplayAttributesId").text == ATTRIBUTE_ID and
			_line("GameplayAttributesLabel").text == DISPLAY_LABEL,
			"The tab loads an earlier v1 definition with its stable identity and label."):
		return false
	_press("GameplayAttributesSave")
	var settings_file: ConfigFile = ConfigFile.new()
	var discard: Button = attributes_tab.find_child("GameplayAttributesDiscard", true, false) as Button
	return _require(settings_file.load(SETTINGS_FILE) == OK and
			settings_file.get_value("crimson", "gameplay_attributes/sets", {}) == expected and
			ProjectSettings.get_setting(SETTING) == expected and discard.disabled,
			"Saving a legacy definition removes obsolete keys and resets the editor's saved draft.")


## Check removal confirmation, discard, and tab re-entry without touching saved definitions.
func _verify_draft_actions() -> bool:
	await _add("GameplayAttributesAddAttribute", "temporary")
	var entries: ItemList = attributes_tab.find_child("GameplayAttributesEntries", true, false) as ItemList
	var remove_button: Button = attributes_tab.find_child("GameplayAttributesRemoveAttribute", true, false) as Button
	remove_button.grab_focus()
	_press("GameplayAttributesRemoveAttribute")
	var escape: InputEventKey = InputEventKey.new()
	escape.keycode = KEY_ESCAPE
	escape.pressed = true
	attributes_tab.get_viewport().push_input(escape, true)
	if not _require(entries.item_count == 2 and not _prompt().visible and remove_button.has_focus() and
			attributes_tab.get_window().visible, "Escape cancels the modal, keeps the draft, and restores focus."):
		return false
	_press("GameplayAttributesRemoveAttribute")
	_press("ConfirmDialog", _prompt())
	if not _require(entries.item_count == 1 and _line("GameplayAttributesId").text == ATTRIBUTE_ID and entries.has_focus(),
			"Remove selects the remaining attribute and focuses the list."):
		return false
	_edit_field("GameplayAttributesLabel", "Draft Label")
	_press("GameplayAttributesDiscard")
	_press("ConfirmDialog", _prompt())
	attributes_tab.get_window().hide()
	await _frames()
	if not await _open_settings():
		return false
	var on_disk: ConfigFile = ConfigFile.new()
	if not _require(on_disk.load(SETTINGS_FILE) == OK, "The actual project settings file can be reopened."):
		return false
	return _require(_line("GameplayAttributesLabel").text == DISPLAY_LABEL and
			on_disk.get_value("crimson", "gameplay_attributes/sets", {}) == ProjectSettings.get_setting(SETTING),
			"Discard restores the saved label and reopening the tab uses the persisted registry.")


## Add registered sets through a scene's native Inspector, then check native history and saving.
func _verify_component() -> bool:
	const SECOND_SET_ID: String = "resources"
	await _add("GameplayAttributesAddSet", SECOND_SET_ID)
	_press("GameplayAttributesSave")
	attributes_tab.get_window().hide()
	var root_node: Node2D = Node2D.new()
	root_node.name = "Player"
	var component: Node = ClassDB.instantiate("AbilitySystemComponent") as Node
	if not _require(component != null, "The editor can create an AbilitySystemComponent node."):
		root_node.free()
		return false
	component.name = "AbilitySystemComponent"
	root_node.add_child(component)
	component.owner = root_node
	var packed: PackedScene = PackedScene.new()
	var packed_ok: bool = packed.pack(root_node) == OK and ResourceSaver.save(packed, COMPONENT_SCENE) == OK
	root_node.free()
	if not _require(packed_ok, "Create a Player scene containing an AbilitySystemComponent."):
		return false
	EditorInterface.open_scene_from_path(COMPONENT_SCENE)
	await _frames()
	var edited_root: Node = EditorInterface.get_edited_scene_root()
	if not _require(edited_root != null, "Open the test scene in the editor."):
		return false
	component = edited_root.get_node("AbilitySystemComponent")
	EditorInterface.get_selection().clear()
	EditorInterface.get_selection().add_node(component)
	EditorInterface.edit_node(component)
	await _frames()
	var inspector: EditorInspector = EditorInterface.get_inspector()
	var row: EditorProperty = inspector.find_child("AbilitySystemAttributeSetsProperty", true, false) as EditorProperty
	if not _require(row != null and row.is_visible_in_tree() and component.get("attribute_source") == 0,
			"The selected component uses the dedicated Attribute Sets control and Local source by default."):
		return false
	if not await _choose_component_set(row, SET_ID) or not await _choose_component_set(row, SECOND_SET_ID):
		return false
	var first_only: PackedStringArray = PackedStringArray([SET_ID])
	var both_sets: PackedStringArray = PackedStringArray([SET_ID, SECOND_SET_ID])
	if not _require(component.get("attribute_sets") == both_sets,
			"The component stores the two stable set IDs in the order assigned."):
		return false
	var history: EditorUndoRedoManager = EditorInterface.get_editor_undo_redo()
	var scene_history: UndoRedo = history.get_history_undo_redo(history.get_object_history_id(component))
	if not _require(scene_history != null and scene_history.undo() and component.get("attribute_sets") == first_only,
			"Undo restores the previous set assignment."):
		return false
	if not _require(scene_history.redo() and component.get("attribute_sets") == both_sets,
			"Redo restores the second set assignment."):
		return false
	await _frames()
	var removals: Array[Node] = row.find_children("*", "Button", true, false)
	for item: Node in removals:
		var button: Button = item as Button
		if button.accessibility_name == "Remove attribute set " + SECOND_SET_ID:
			button.grab_focus()
			button.pressed.emit()
			break
	await _frames()
	var trigger: Button = row.find_child("AddAttributeSet", true, false) as Button
	if not _require(component.get("attribute_sets") == first_only and trigger.has_focus(),
			"Remove returns focus to Add Attribute Set."):
		return false
	EditorInterface.save_scene_as(COMPONENT_SCENE, false)
	var saved_scene: PackedScene = (ResourceLoader.load(COMPONENT_SCENE, "PackedScene",
			ResourceLoader.CACHE_MODE_IGNORE) as PackedScene)
	if not _require(saved_scene != null, "The editor saves the packed scene."):
		return false
	var saved_root: Node = saved_scene.instantiate()
	var saved_component: Node = saved_root.get_node("AbilitySystemComponent")
	var persisted: bool = saved_component.get("attribute_sets") == first_only
	saved_root.free()
	var inspector_area: Rect2 = inspector.get_global_rect()
	inspector_area.end.y = row.get_global_rect().end.y + 64.0 * EditorInterface.get_editor_scale()
	return (_require(persisted, "Reloading the packed scene preserves the assignment.") and
			_capture(inspector.get_viewport(), COMPONENT_CAPTURE, Rect2i(inspector_area)))


## Use the shared searchable picker and native Enter-to-select path.
func _choose_component_set(row: EditorProperty, set_name: String) -> bool:
	_press("AddAttributeSet", row)
	await _frames()
	var popups: Array[Node] = row.find_children("*", "PopupPanel", true, false)
	var popup: PopupPanel = popups.front() as PopupPanel if not popups.is_empty() else null
	if not _require(popup != null and popup.visible and popup.gui_get_focus_owner() is LineEdit,
			"The Attribute Sets picker opens with its search entry focused."):
		return false
	var search: LineEdit = popup.gui_get_focus_owner() as LineEdit
	search.text = set_name
	search.text_changed.emit(set_name)
	search.text_submitted.emit(set_name)
	await _frames()
	return _require(not popup.visible, "Choosing a registered set closes the popup.")


## Capture the real editor viewport when run with rendering enabled.
func _capture(view: Viewport, path: String, region: Rect2i = Rect2i()) -> bool:
	if "--capture-attributes-ui" not in OS.get_cmdline_user_args():
		return true
	RenderingServer.force_draw()
	var capture_image: Image = view.get_texture().get_image()
	if not _require(capture_image != null and not capture_image.is_empty(), "Read the rendered editor viewport."):
		return false
	if region.has_area():
		capture_image = capture_image.get_region(region.intersection(Rect2i(Vector2i.ZERO, capture_image.get_size())))
	return _require(not capture_image.is_empty() and capture_image.save_png(path) == OK,
			"Capture the visible editor at " + path + ".")


## Resolve the active scoped editor modal.
func _prompt() -> Control:
	return attributes_tab.find_child("GameplayAttributesPrompt", true, false) as Control


## Enter an identity using the editor modal's keyboard submission path.
func _submit_id(row_id: String) -> void:
	var id_entry: LineEdit = _line("GameplayAttributesPromptId", _prompt())
	id_entry.text = row_id
	id_entry.text_changed.emit(row_id)
	id_entry.text_submitted.emit(row_id)


## Trigger an add command and allow the tab to update its form and selection.
func _add(action_name: String, row_id: String) -> void:
	_press(action_name)
	_submit_id(row_id)
	await _frames()


## Read a stable field in the current tab or modal.
func _line(field_name: String, scope: Node = null) -> LineEdit:
	var search_scope: Node = scope if scope != null else attributes_tab
	return search_scope.find_child(field_name, true, false) as LineEdit


## Send a live text-edit signal using the same control contract as keyboard editing.
func _edit_field(field_name: String, value_text: String) -> void:
	var editor: LineEdit = _line(field_name)
	editor.text = value_text
	editor.text_changed.emit(value_text)


## Run the same button action used by pointer and keyboard activation.
func _press(action_name: String, scope: Node = null) -> void:
	var search_scope: Node = scope if scope != null else attributes_tab
	var action_button: Button = search_scope.find_child(action_name, true, false) as Button
	if _require(action_button != null and action_button.is_visible_in_tree() and not action_button.disabled,
			"Editor action is available: " + action_name):
		action_button.pressed.emit()


## Read only this fixture's authored project setting.
func _saved_sets() -> Array:
	var registry: Dictionary = ProjectSettings.get_setting(SETTING)
	return registry.get("sets", [])


## Read the authored fixture attribute from persisted settings.
func _saved_attribute() -> Dictionary:
	var saved_sets: Array = _saved_sets()
	if saved_sets.size() != 1:
		return {}
	var rows: Array = saved_sets.front().get("attributes", [])
	return rows.front() if rows.size() == 1 else {}


## Wait for native editor controls to finish their deferred layout and focus work.
func _frames() -> void:
	for frame_index: int in range(8):
		await get_tree().process_frame


## Report one deterministic failure and stop the isolated editor.
func _require(condition: bool, message: String) -> bool:
	if not condition:
		push_error("[Crimson] Gameplay Attributes editor fixture. " + message)
		get_tree().quit(1)
	return condition
