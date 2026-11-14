<!-- Copyright Francesco Di Blasi 2026 - All Rights Reserved -->

# Rebinding

Open an editor for the same [Controls scope](../README.md) used by gameplay. Pass the settings node to tie capture to its lifetime. The editor changes the current player's runtime binding overlay.

```gdscript
var bindings := Rebinding.open(controls, self)
var forward := bindings.slot_by_id(&"move/key_up")
var result: RebindResult = await bindings.rebind(forward)
if result.ok:
    %MoveForward.text = result.slot.label
else:
    %Status.text = result.message
```

`slots()` returns all editable physical event slots in layout order. `slot(action, Rebinding.KEYBOARD_MOUSE)` or `slot(action, Rebinding.GAMEPAD)` returns the first matching slot. Use a stable slot ID for an exact direction or alternative. Whole-stick sources use a pair of axes and are edited in the map resource. Signed individual axis slots are available for player rebinding. `BindingSlot` is a snapshot. Read it again after changes or use the new snapshot on a successful result. Its `event` property returns a detached event copy. Use the `changed` signal for edits by that editor. Its slot is null after a bulk change.

Capture temporarily suspends gameplay on the selected Controls scope. While listening, the editor consumes eligible raw button and axis events before GUI processing. Use Escape, controller Back, or `cancel()` to dismiss it. Window focus loss, owner exit, and Controls retirement also cancel it. A capture expires after 15 seconds. A held controller axis must return to neutral before it can bind. A modifier by itself binds on release, and a key pressed with a modifier binds a chord. A conflict returns the old binding unchanged.

`reset(slot)` restores that slot's authored value if the resulting binding is valid. `reset_all()` restores the authored layout. Call `save("user://controls.json")` to write overrides explicitly on Windows, then `load("user://controls.json")` to restore them. Loads validate the whole profile before changing any slots and restore omitted slots to their authored defaults. An unknown slot ID, invalid event, or conflict leaves the current overlay intact. Reset, save, and load report busy when any editor is capturing in this Controls scope. The settings UI chooses when to save and which profile path to use.
