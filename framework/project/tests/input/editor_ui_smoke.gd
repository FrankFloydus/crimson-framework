# Copyright Francesco Di Blasi 2026 - All Rights Reserved

@tool
extends EditorPlugin

## Paths in the isolated project owned by the test harness.
const MAP_PATH := "res://editor_ui_smoke_map.tres"
const ACTION_PATH := "res://editor_ui_smoke_indexed_action.tres"
const AUTHORED_KEY: Key = KEY_W
const REPLACEMENT_KEY: Key = KEY_J


## Wait for Crimson to register its native tool windows and inspector controls.
func _enter_tree() -> void:
	_verify.call_deferred()


## Enter through the existing tool windows and run a focused check of each workspace.
func _verify() -> void:
	await get_tree().process_frame
	await get_tree().process_frame
	if not await _verify_catalog() or not await _verify_map():
		return
	print("CRIMSON_SHARED_EDITOR_UI_PASSED")
	get_tree().quit()


## Verify catalog interactions and the connected native graph specimens.
func _verify_catalog() -> bool:
	var catalog := get_tree().root.find_child("CrimsonComponentCatalog", true, false) as Window
	if not _require(catalog != null, "Find the component catalog registered by the add-on."):
		return false
	catalog.popup_centered(Vector2i(900, 570))
	if not await _verify_design_style(catalog):
		return false
	if not await _verify_catalog_controls(catalog):
		return false
	var graph_menu := await _open_catalog_page(catalog, "Graph Node")
	if not _require(graph_menu != null, "Find the Graph Node page in the catalog."):
		return false
	if not _require(graph_menu.button_pressed, "The current catalog page stays selected after the content changes."):
		return false

	var canvas := catalog.find_child("CatalogGraphNodeSample", true, false) as GraphEdit
	if not _require(canvas != null and canvas.get_connection_list().size() == 2,
			"The sample contains a native execution wire and data wire."):
		return false
	var source_card := _graph_card(canvas, "On Hit")
	var target_card := _graph_card(canvas, "Apply Damage")
	if not _require(source_card != null and target_card != null, "Find the event and call specimens by title."):
		return false
	var flow_type := source_card.get_output_port_type(0)
	var data_type := source_card.get_output_port_type(1)
	if not _require(flow_type != data_type and flow_type == target_card.get_input_port_type(0)
			and data_type == target_card.get_input_port_type(1), "The graph distinguishes flow and typed data."):
		return false
	var minimum_before_selection := target_card.get_combined_minimum_size()
	canvas.set_selected(target_card)
	await get_tree().process_frame
	if not _require(target_card.selected and target_card.get_combined_minimum_size() == minimum_before_selection
			and canvas.get_connection_list().size() == 2,
			"Selecting a graph sample keeps its ports and card dimensions stable."):
		return false
	var variable_canvas := catalog.find_child("CatalogGraphVariableSample", true, false) as GraphEdit
	if not _require(variable_canvas != null and variable_canvas.get_connection_list().size() == 1,
			"The catalog has a separate native read/write sample connected with a typed wire."):
		return false
	var getter := _graph_card(variable_canvas, "Get shield")
	var setter := _graph_card(variable_canvas, "Set shield")
	if not _require(getter != null and setter != null and getter.get_output_port_count() == 1
			and getter.get_input_port_count() == 0 and setter.get_input_port_count() == 1
			and getter.get_output_port_type(0) == setter.get_input_port_type(0),
			"Both variable card forms keep their typed value ports."):
		return false
	catalog.hide()
	return true


## Verify that the native guide reads the bundled standards and can return to the components.
func _verify_design_style(catalog: Window) -> bool:
	var navigation := catalog.find_child("CatalogNavigation", true, false) as TabContainer
	if not _require(navigation != null and navigation.get_tab_count() == 2
			and navigation.get_tab_control(0) != null and navigation.get_tab_control(1) != null,
			"The component catalog exposes a Design Style tab beside its examples."):
		return false
	await get_tree().process_frame
	var strip := navigation.get_tab_bar()
	var guide_point := strip.get_global_transform_with_canvas() * strip.get_tab_rect(1).get_center()
	_click_in_window(catalog, guide_point)
	await get_tree().process_frame
	var guide := navigation.find_child("DesignStyleGuide", true, false) as Control
	if not _require(navigation.current_tab == 1 and guide != null and guide.is_visible_in_tree()
			and not navigation.get_tab_control(0).is_visible_in_tree(), "Opening Design Style displays only the standards."):
		return false
	if not _require(guide.get_child_count() > 1 and not guide.find_children("*", "Label", true, false).is_empty(),
			"The native Design Style guide displays its bundled sections."):
		return false
	var components_point := strip.get_global_transform_with_canvas() * strip.get_tab_rect(0).get_center()
	_click_in_window(catalog, components_point)
	await get_tree().process_frame
	return _require(navigation.current_tab == 0 and not guide.is_visible_in_tree()
			and navigation.get_tab_control(0).is_visible_in_tree(), "Components can be reopened from Design Style.")


func _graph_card(canvas: GraphEdit, card_title: String) -> GraphNode:
	for candidate in canvas.find_children("*", "GraphNode", false, false):
		var card := candidate as GraphNode
		if card != null and card.title == card_title:
			return card
	return null


## Exercise native editing and keyboard navigation through each catalog control.
func _verify_catalog_controls(catalog: Window) -> bool:
	if not await _open_catalog_page(catalog, "Button") or not await _verify_button(catalog):
		return false
	if not await _open_catalog_page(catalog, "Text Input") or not await _verify_text_edit(catalog):
		return false
	if not await _open_catalog_page(catalog, "Checkbox") or not await _verify_choice(catalog, "CheckBox"):
		return false
	if not await _open_catalog_page(catalog, "Switch") or not await _verify_choice(catalog, "CheckButton"):
		return false
	if not await _open_catalog_page(catalog, "Select") or not await _verify_select(catalog):
		return false
	if not await _open_catalog_page(catalog, "Combobox") or not await _verify_searchable_picker(catalog):
		return false
	if not await _open_catalog_page(catalog, "Tabs"):
		return false
	return await _verify_tabs(catalog)


func _open_catalog_page(catalog: Window, caption: String) -> Button:
	var tab := _button(catalog, caption)
	if not _require(tab != null, "Find the %s component sample." % caption):
		return null
	tab.button_pressed = true
	tab.pressed.emit()
	for frame in range(2):
		await get_tree().process_frame
	return tab


## Filter the native searchable picker through its real text input and choose a visible result.
func _verify_searchable_picker(catalog: Window) -> bool:
	var trigger := _button(catalog, "Find an action")
	if not _require(trigger != null and not trigger.disabled, "Find the interactive searchable picker."):
		return false
	trigger.pressed.emit()
	await get_tree().process_frame
	var popups := trigger.get_parent().find_children("*", "PopupPanel", false, false)
	var popup := popups[0] as PopupPanel if not popups.is_empty() else null
	var search_fields := popup.find_children("*", "LineEdit", true, false) if popup != null else []
	var search := search_fields[0] as LineEdit if not search_fields.is_empty() else null
	if not _require(popup != null and popup.visible and search != null and search.has_focus(),
			"Opening the searchable picker focuses its query."):
		return false

	search.text = "__crimson_fixture_no_matching_picker_option__"
	search.text_changed.emit(search.text)
	await get_tree().process_frame
	search.edit()
	_press_in_window(popup, KEY_ENTER)
	if not _require(popup.visible and search.has_focus(), "An empty query result neither commits nor closes the picker."):
		return false

	var found := "Jump"
	search.text = found
	search.text_changed.emit(search.text)
	await get_tree().process_frame
	if not _require(_button(popup, found) != null and search.has_focus(),
			"Filtering restores the matching result while preserving the search caret."):
		return false
	search.edit()
	_press_in_window(popup, KEY_ENTER)
	await get_tree().process_frame
	return _require(not popup.visible and trigger.text == found and trigger.has_focus(),
			"Enter selects the filtered result and returns focus to the picker trigger.")


## A focused native button dispatches its command.
func _verify_button(catalog: Window) -> bool:
	var command := _button(catalog, "Save action")
	if not _require(command != null and not command.disabled, "Find an enabled native command."):
		return false
	command.grab_focus()
	var calls: Array[bool] = []
	command.pressed.connect(func() -> void: calls.append(true))
	_press_in_window(catalog, KEY_SPACE)
	await get_tree().process_frame
	return _require(command.has_focus() and calls.size() == 1, "A focused button handles one keyboard activation.")


## Each independent choice toggles through the native button input route.
func _verify_choice(catalog: Window, class_name_filter: String) -> bool:
	for candidate in catalog.find_children("*", class_name_filter, true, false):
		var choice := candidate as BaseButton
		if choice == null or choice.disabled or not choice.is_visible_in_tree():
			continue
		choice.grab_focus()
		var original := choice.button_pressed
		var changes: Array[bool] = []
		choice.toggled.connect(func(value: bool) -> void: changes.append(value))
		_press_in_window(catalog, KEY_SPACE)
		await get_tree().process_frame
		return _require(choice.has_focus() and choice.button_pressed != original
				and changes == [not original], "One keyboard activation changes the choice and emits its value.")
	return _require(false, "Find an editable native %s sample." % class_name_filter)


func _verify_select(catalog: Window) -> bool:
	var trigger := _button(catalog, "Pressed")
	var picker := trigger.get_parent() if trigger != null else null
	var popup := picker.get_node_or_null(^"SelectPopup") as PopupPanel if picker != null else null
	if not _require(trigger != null and trigger.name == &"SelectTrigger" and popup != null,
			"Find the live compact Select trigger and popup."):
		return false
	trigger.pressed.emit()
	for frame in range(3):
		await get_tree().process_frame
	var selected := _button(popup, "Pressed")
	var released := _button(popup, "Released")
	var held := _button(popup, "Held")
	if not _require(popup.visible and selected != null and released != null and held != null,
			"Clicking Select opens all options (trigger %s, popup %s, options found %s/%s/%s)."
			% [trigger.get_global_rect(), popup.visible, selected != null, released != null, held != null]):
		return false
	if not _require(popup.get_visible_rect().size.x <= catalog.get_visible_rect().size.x,
			"The Select popup stays within the workbench width."):
		return false
	if not _verify_selection_mark(selected, true) or not _verify_selection_mark(released, false):
		return false

	_press_in_window(popup, KEY_DOWN)
	await get_tree().process_frame
	if not _require(released.has_focus() and trigger.text == "Pressed",
			"Down moves to the next option without changing the selected value."):
		return false
	_press_in_window(popup, KEY_UP)
	await get_tree().process_frame
	if not _require(selected.has_focus(), "Up returns to the previous option."):
		return false
	_press_in_window(popup, KEY_DOWN)
	_press_in_window(popup, KEY_ENTER)
	await get_tree().process_frame
	if not _require(not popup.visible and trigger.text == "Released" and trigger.has_focus(),
			"Enter chooses the active option, closes the popup, and returns keyboard focus."):
		return false
	trigger.pressed.emit()
	for frame in range(3):
		await get_tree().process_frame
	if not _verify_selection_mark(_button(popup, "Released"), true):
		return false
	_press_in_window(popup, KEY_DOWN)
	# Headless provides no platform event sender for the native Popup Escape callback.
	if DisplayServer.get_name() == "headless":
		popup.hide()
	else:
		_press_in_window(catalog, KEY_ESCAPE)
	await get_tree().process_frame
	if not _require(not popup.visible and trigger.text == "Released" and trigger.has_focus(),
			"Canceling closes the menu and preserves the chosen value."):
		return false
	trigger.pressed.emit()
	await get_tree().process_frame
	held = _button(popup, "Held")
	if not _require(popup.visible and held != null, "The Select can be reopened after Escape."):
		return false
	if DisplayServer.get_name() == "headless":
		held.pressed.emit()
	else:
		_click_in_window(popup, held.get_global_rect().get_center())
	await get_tree().process_frame
	return _require(not popup.visible and trigger.text == "Held", "Activating an option commits its value.")


func _verify_selection_mark(option: Button, chosen: bool) -> bool:
	var mark := option.get_parent().get_node_or_null(^"SelectedCheck") as TextureRect if option != null else null
	if not _require(mark != null and mark.texture != null and mark.is_visible_in_tree() == chosen
			and option.button_pressed == chosen, "Only the chosen Select option shows a mark."):
		return false
	if not chosen:
		return true
	# The headless display driver has no native popup drawing surface. Verify its right anchor there.
	if DisplayServer.get_name() == "headless":
		return _require(mark.get_anchor(SIDE_LEFT) == 1.0 and mark.get_anchor(SIDE_RIGHT) == 1.0
				and mark.get_offset(SIDE_LEFT) < mark.get_offset(SIDE_RIGHT) and mark.get_offset(SIDE_RIGHT) < 0.0,
				"The chosen mark anchors to the trailing edge of its option.")
	var row_rect := option.get_global_rect()
	var mark_rect := mark.get_global_rect()
	return _require(row_rect.encloses(mark_rect) and mark_rect.get_center().x > row_rect.get_center().x,
			"The Select mark is contained at its trailing edge (row %s, mark %s)." % [row_rect, mark_rect])


func _click_in_window(window: Window, point: Vector2) -> void:
	for is_pressed in [true, false]:
		var click := InputEventMouseButton.new()
		click.button_index = MOUSE_BUTTON_LEFT
		click.button_mask = MOUSE_BUTTON_MASK_LEFT if is_pressed else 0
		click.position = point
		click.global_position = point
		click.pressed = is_pressed
		window.push_input(click, true)


func _press_in_window(window: Window, key: Key) -> void:
	for is_pressed in [true, false]:
		var key_input := InputEventKey.new()
		key_input.keycode = key
		key_input.physical_keycode = key
		key_input.pressed = is_pressed
		window.push_input(key_input)


## An editable native field accepts input from its focused window.
func _verify_text_edit(catalog: Window) -> bool:
	for candidate in catalog.find_children("*", "LineEdit", true, false):
		var input_field := candidate as LineEdit
		if input_field == null or not input_field.editable or input_field.text.is_empty() or not input_field.is_visible_in_tree():
			continue
		input_field.grab_focus()
		var previous_length := input_field.text.length()
		input_field.caret_column = previous_length
		_press_in_window(catalog, KEY_BACKSPACE)
		await get_tree().process_frame
		return _require(input_field.has_focus() and input_field.text.length() < previous_length,
				"The native field accepts keyboard edits while focused.")
	return _require(false, "Find an editable native text field.")


func _verify_tabs(catalog: Window) -> bool:
	for candidate in catalog.find_children("*", "TabContainer", true, false):
		var tab_group := candidate as TabContainer
		if tab_group == null or tab_group.name == &"CatalogNavigation" or tab_group.get_tab_count() != 2 or not tab_group.is_visible_in_tree():
			continue
		var tab_strip := tab_group.get_tab_bar()
		var keyboard := tab_group.get_tab_control(0)
		var gamepad := tab_group.get_tab_control(1)
		if not _require(tab_strip != null and keyboard != null and gamepad != null,
				"Find the native TabBar and its two content panels."):
			return false
		var original_tab := tab_group.current_tab
		var next_tab := 1 - original_tab
		if not _require(keyboard.is_visible_in_tree() == (original_tab == 0)
				and gamepad.is_visible_in_tree() == (original_tab == 1),
				"The initial tab shows its corresponding content panel only."):
			return false
		var point := tab_strip.get_global_transform_with_canvas() * tab_strip.get_tab_rect(next_tab).get_center()
		if not _require(tab_strip.get_global_rect().has_point(point) and catalog.get_visible_rect().has_point(point),
				"The other tab is on-screen and can receive a click."):
			return false
		_click_in_window(catalog, point)
		await get_tree().process_frame
		return _require(tab_group.current_tab == next_tab and tab_strip.current_tab == next_tab
				and keyboard.is_visible_in_tree() == (next_tab == 0)
				and gamepad.is_visible_in_tree() == (next_tab == 1),
				"Clicking a native tab switches to the corresponding content panel.")
	return _require(false, "Find the two-panel native Tabs specimen.")


## Create an editable map with one known physical binding and a separate searchable asset.
func _author_map() -> InputActionMap:
	var indexed_action := InputAction.create(InputAction.BUTTON)
	indexed_action.display_name = "Smoke Indexed Action"
	if not _require(ResourceSaver.save(indexed_action, ACTION_PATH) == OK, "Save the test-owned action asset."):
		return null
	var digital_event := InputEventKey.new()
	digital_event.physical_keycode = AUTHORED_KEY
	var swap_axis := InputSwapAxis.new()
	swap_axis.order = InputSwapAxis.YX
	var binding := InputBinding.new()
	binding.id = &"smoke/move"
	binding.event = digital_event
	binding.modifiers = [swap_axis]
	var action := InputAction.create(InputAction.VECTOR)
	action.display_name = "Smoke Movement"
	var input_map := InputActionMap.create(&"editor_ui_smoke")
	input_map.add_action(&"smoke_move", action).add_binding(binding)
	if not _require(input_map.validate() == OK and ResourceSaver.save(input_map, MAP_PATH) == OK,
			"Save a valid test-owned map."):
		return null
	var editor_filesystem := EditorInterface.get_resource_filesystem()
	while editor_filesystem.is_scanning():
		await get_tree().process_frame
	editor_filesystem.update_file(ACTION_PATH)
	return ResourceLoader.load(MAP_PATH, "InputActionMap", ResourceLoader.CACHE_MODE_IGNORE) as InputActionMap


## Select a binding, release capture on focus exit, and reassign it using the editor's input route.
func _verify_map() -> bool:
	var authored_map := await _author_map()
	if not _require(authored_map != null, "Load the test-owned input map."):
		return false
	EditorInterface.inspect_object(authored_map)
	await get_tree().process_frame
	await get_tree().process_frame
	var open_map: Button = _button(EditorInterface.get_inspector(), "Edit Input Map")
	if not _require(open_map != null, "Open the map through its inspector button."):
		return false
	open_map.pressed.emit()
	await get_tree().process_frame
	var map_window := get_tree().root.find_child("CrimsonInputMap", true, false) as Window
	if not _require(map_window != null and map_window.visible, "Show the Input Map workspace."):
		return false
	var bindings: Node = map_window.find_child("ActionBindingTree", true, false)
	var search_field := map_window.find_child("InputMapSearch", true, false) as LineEdit
	var input_status := map_window.find_child("InputStatus", true, false) as Label
	if not _require(bindings != null and search_field != null and search_field.right_icon != null and input_status != null,
			"The map has a binding tree, shared search and capture feedback."):
		return false
	if not await _verify_action_asset_chooser(map_window):
		return false
	var capture: Button = _button(bindings, "", "Listen for input.")
	if not _require(capture != null, "Find the first physical binding capture control."):
		return false
	var initial_key_label := OS.get_keycode_string(AUTHORED_KEY)
	var replacement_key_label := OS.get_keycode_string(REPLACEMENT_KEY)
	if not await _verify_narrow_binding_row(map_window, bindings, capture, initial_key_label):
		return false
	capture.grab_focus(true)
	capture.pressed.emit()
	await get_tree().process_frame
	var modifiers: Node = map_window.find_child("BindingModifiers", true, false)
	var add_modifier: Button = _button(modifiers, "Add modifier") if modifiers != null else null
	if not _require(not input_status.text.is_empty() and add_modifier != null,
			"Selecting a key binding exposes its modifiers and active capture hint."):
		return false
	if not _require(not add_modifier.clip_text and add_modifier.icon != null and
			add_modifier.size.x >= add_modifier.get_combined_minimum_size().x,
			"The modifier action fits its whole caption and dropdown icon."):
		return false
	map_window.focus_exited.emit()
	map_window.push_input(_key_press(), true)
	await get_tree().process_frame
	if not _require(input_status.text.is_empty() and _button(bindings, replacement_key_label) == null,
			"Focus loss cancels capture, so the next key does not change the draft."):
		return false
	capture.grab_focus()
	if not _require(capture.has_focus(), "The binding device command still accepts keyboard focus."):
		return false
	capture.pressed.emit()
	if not _require(not input_status.text.is_empty(), "The keyboard-focused device command still starts capture."):
		return false
	capture.pressed.emit()
	if not _require(capture.has_focus(), "Canceling capture returns keyboard focus to the same command."):
		return false
	capture.pressed.emit()
	map_window.push_input(_key_press(), true)
	await get_tree().process_frame
	if not _require(_button(bindings, replacement_key_label) != null,
			"The capture can be restarted and updates the same row when the key is pressed."):
		return false
	if not await _verify_compact_modifier_selectors(modifiers):
		return false
	map_window.hide()
	return true


## Browse indexed action assets and keep a filtered or canceled chooser from mutating the draft.
func _verify_action_asset_chooser(map_window: Window) -> bool:
	var action_command := map_window.find_child("AddInputAction", true, false) as MenuButton
	if not _require(action_command != null, "Find Add Action in the map workspace."):
		return false
	var original_title := map_window.title
	action_command.get_popup().id_pressed.emit(action_command.get_popup().get_item_id(0))
	await get_tree().process_frame
	var chooser := map_window.find_child("ChooseInputAction", true, false) as Control
	if not _require(chooser != null and chooser.visible, "Use the workbench's action asset chooser."):
		return false
	var query := chooser.find_child("InputActionAssetQuery", true, false) as LineEdit
	var results := chooser.find_child("InputActionAssetResults", true, false) as Tree
	var confirm := chooser.find_child("ConfirmDialog", true, false) as Button
	if not _require(query != null and query.has_focus() and results != null and confirm != null,
			"The chooser starts in its search field with results and an add command."):
		return false
	query.text = ACTION_PATH.trim_prefix("res://").get_basename()
	query.text_changed.emit(query.text)
	var selected_asset := results.get_selected()
	if not _require(results.visible and selected_asset != null and not confirm.disabled
			and String(selected_asset.get_metadata(0)) == ACTION_PATH,
			"Search by folder and name resolves the canonical path of the authored action asset."):
		return false
	query.text = "missing-from-input-asset-index-test"
	query.text_changed.emit(query.text)
	if not _require(not results.visible and confirm.disabled, "No matching actions disable Add Action."):
		return false
	var cancel_event := InputEventKey.new()
	cancel_event.keycode = KEY_ESCAPE
	cancel_event.pressed = true
	map_window.push_input(cancel_event, true)
	await get_tree().process_frame
	return _require(not chooser.visible and map_window.title == original_title,
			"Escape leaves the draft unchanged.")


## Changing one axis through the compact selector updates the live binding and the opposite field.
func _verify_compact_modifier_selectors(modifiers: Node) -> bool:
	var output_x := _button(modifiers, "Y", "Choose the source axis for horizontal output.")
	if not _require(output_x != null, "Find the current Swap Axis selector on the edited binding."):
		return false
	output_x.pressed.emit()
	await get_tree().process_frame
	var options := output_x.get_parent().find_children("*", "PopupPanel", false, false)
	var popup_panel := options[0] as PopupPanel if not options.is_empty() else null
	var swap_to_x := _button(popup_panel, "X") if popup_panel != null else null
	if not _require(popup_panel != null and popup_panel.visible and swap_to_x != null,
			"The compact selector opens without leaving the Input Map."):
		return false
	swap_to_x.pressed.emit()
	await get_tree().process_frame
	return _require(_button(modifiers, "X", "Choose the source axis for horizontal output.") != null
			and _button(modifiers, "Y", "Choose the source axis for vertical output.") != null,
			"The selected index changes output X and keeps the opposite source unique.")


## Resizing to the workbench minimum keeps all row actions available without rebuilding the tree.
func _verify_narrow_binding_row(map_window: Window, bindings: Node, capture: Button, key_label: String) -> bool:
	var old_size := map_window.size
	map_window.size = map_window.min_size
	for layout_frame in range(3):
		await get_tree().process_frame
	var row_content := capture.get_parent()
	var row_overlay := row_content.get_parent()
	var row_target := row_overlay.get_parent() as Button
	var binding_picker := _button(row_content, key_label)
	var remove_binding := _button(row_overlay, "", "Remove this binding.")
	if not _require(_button(bindings, "", "Listen for input.") == capture and row_target != null
			and binding_picker != null and remove_binding != null,
			"Resizing the workbench leaves the capture, dropdown and remove controls on the same binding row."):
		return false
	for row_action: Button in [capture, binding_picker, remove_binding]:
		var row_bounds := row_target.get_global_rect().grow(1.0)
		if not _require(row_bounds.encloses(row_action.get_global_rect()) and row_action.focus_mode == Control.FOCUS_ALL,
				"All binding actions fit inside the narrow row and can receive keyboard or controller focus."):
			return false
	map_window.size = old_size
	await get_tree().process_frame
	return true


## Find an editor command through its visible text or the one shared device tooltip.
func _button(group: Node, caption: String, tooltip_prefix: String = "") -> Button:
	for candidate: Node in group.find_children("*", "Button", true, false):
		var command := candidate as Button
		if command.text == caption and (tooltip_prefix.is_empty() or command.tooltip_text.begins_with(tooltip_prefix)):
			return command
	return null


## Make an input that represents a physical key reaching the current tool window.
func _key_press() -> InputEventKey:
	var event := InputEventKey.new()
	event.keycode = REPLACEMENT_KEY
	event.physical_keycode = REPLACEMENT_KEY
	event.pressed = true
	return event


## Report fixture failures in the harness log before closing the isolated editor.
func _require(condition: bool, requirement: String) -> bool:
	if not condition:
		push_error("[Crimson] Shared editor UI fixture: " + requirement)
		get_tree().quit(1)
	return condition
