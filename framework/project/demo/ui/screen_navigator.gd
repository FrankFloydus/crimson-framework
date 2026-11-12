# Copyright Francesco Di Blasi 2026 - All Rights Reserved

extends Control
## Shows one mounted screen at a time and restores a usable focus target on return.
## Screens are direct child Controls and callers decide when to navigate.


class ScreenEntry:
	var page: Control
	var default_focus: Control
	var last_focus: Control

	func _init(screen: Control, first_focus: Control) -> void:
		page = screen
		default_focus = first_focus


var _history: Array[ScreenEntry] = []


func _ready() -> void:
	for child in get_children():
		if child is Control:
			child.hide()
	get_viewport().gui_focus_changed.connect(_remember_focus)


## Present the root screen and discard its previous navigation history.
func reset(page: Control, initial_focus: Control) -> void:
	if not _history.is_empty():
		_history.back().page.hide()
	_history.clear()
	push(page, initial_focus)


## Present a new screen and remember the current screen's most recent focus.
func push(page: Control, initial_focus: Control) -> void:
	assert(page.get_parent() == self)
	for entry in _history:
		if entry.page == page:
			return
	if not _history.is_empty():
		_history.back().page.hide()
	_history.append(ScreenEntry.new(page, initial_focus))
	page.show()
	restore_focus()


## Reveal the previous screen. Return false when already at the root.
func pop() -> bool:
	if _history.size() < 2:
		return false
	var previous: ScreenEntry = _history.pop_back()
	previous.page.hide()
	_history.back().page.show()
	restore_focus()
	return true


## Use remembered focus, the default, or the first eligible descendant.
func restore_focus() -> void:
	if _history.is_empty() or not is_visible_in_tree():
		return
	var entry: ScreenEntry = _history.back()
	for target in [entry.last_focus, entry.default_focus]:
		if is_instance_valid(target) and _can_focus(entry.page, target):
			target.grab_focus()
			return
	for target: Control in entry.page.find_children("*", "Control", true, false):
		if _can_focus(entry.page, target):
			target.grab_focus()
			return


func _remember_focus(target: Control) -> void:
	if _history.is_empty():
		return
	var entry: ScreenEntry = _history.back()
	if _can_focus(entry.page, target):
		entry.last_focus = target



func _can_focus(page: Control, target: Control) -> bool:
	if target == null or not page.is_ancestor_of(target) or not target.is_visible_in_tree():
		return false
	if target.get_focus_mode_with_override() != Control.FOCUS_ALL:
		return false
	return not (target is BaseButton and target.disabled)
