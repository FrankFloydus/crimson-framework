<!-- Copyright Francesco Di Blasi 2026 - All Rights Reserved -->

# Author an Input Map in Godot

An **InputAction** defines what an action returns, such as a button, axis, vector, or relative mouse motion. An **InputActionMap** gives a set of actions their gameplay IDs and physical bindings. An **InputLayout** collects the maps used by one player and holds the player's pointer policy.

## Open a map

- Select a saved **InputActionMap** resource in the FileSystem and click **Edit Input Map** in the Inspector.
- Select the player's **Controls** component and click **Edit Input Map**. If the assigned layout has several maps, choose the one you want to edit.
- Open **Project → Tools → Input Maps**. Use the **Map** menu in the footer to open an existing map or start a new draft.

The window shows one map. In **Actions**, use the filter to find a name or a physical control. Each action group shows its value type and lists one row per binding in authored order. On the right, the selected action and physical control stay visible above the scrollable **Modifiers** stack. Drag the divider or focus it and use the arrow keys to resize the panes.

An older layout with inline actions opens the compatibility editor. Its fields use the same controls for action types, maps, binding directions, and presets. The manual physical-control chooser retains the saved device mappings for older layouts.

## Change the demo jump key

1. Open the demo player scene. Select **Controls → Edit Input Map** to open **On Foot**.
2. Under **Jump**, click the keyboard icon next to **Space Bar** and press **J**. The same row updates to **J**. Press **Esc** to cancel capture.
3. Click **Save** in the footer and start a new session. If the local player already has a saved override for this slot, reset the override in the demo settings before trying **J**.

You can also choose **J** from the binding dropdown. The dropdown searches expandable **Keyboard**, **Mouse**, and **Gamepad** groups. It only offers sources compatible with the action's value type. Choose **None** to leave the slot unassigned. Picking or listening keeps the slot ID, so saved rebinding data still points to the same row.

For a **Mouse Motion** action, pick **Mouse X (Horizontal)**, **Mouse Y (Vertical)**, or **Mouse XY (Both)**. The selected axes carry relative motion in pixels. Listening on a new mouse-motion row captures both axes. Listening on an existing row keeps the selected axes. You can bind a **Left Stick** or **Right Stick** as one 2D control, or pick a signed stick direction as one source.

## Set up a new layout

1. To start from a reusable preset, choose **Map → Open FPS Starter**. It supplies default movement, jumping, mouse look, and stick look. Save your own copy before changing it.
2. To add gameplay-specific input, use **Add Action → Choose Action Asset**. Search project **InputAction** assets by name or folder, then select a result and click **Add Action**. The same definition can appear more than once with different action IDs. Choose **New Action Asset** when you need a definition with its own value type.
3. The map supplies a script ID based on the action name. To change it deliberately, open the action row's menu and choose **Edit Action ID**. Other maps that use the same definition keep their own IDs and bindings.
4. Click the **+** beside the action to add a binding. Choose a control from the dropdown or use the device button to listen.
5. Save the map as a .tres resource. In the **InputLayout** Inspector, add it to **Maps**, then assign the layout to **Input Layout** on your Controls component. If you opened the editor from Controls with no layout or from an empty layout, opening or first saving a valid map assigns it for you.

**Map → Map Settings** changes the display name or the stable map ID. **Gameplay** is enabled when Controls boots. Use **Save a Copy** to make a separate map asset, then add the copy to a layout. The copy keeps the same IDs initially. Map, action, and binding IDs must be unique within a layout.

## Edit a binding's values

Select a binding row to inspect it. Drag the handle on an action or binding to move it before or after a sibling. With the handle focused, press **Alt+Up** or **Alt+Down** to move it one place. Clear the action search to reorder the full list. Bindings stay within their own action.

A new keyboard or other single-source binding on a 2D action starts on positive X. Add **Swap Axis** to move that value to Y, then **Invert** to reverse the output direction. For example, on **Move**, a D binding needs no modifiers. A uses **Invert X**, S uses **Swap Axis**, and W uses **Swap Axis** followed by **Invert Y**.

| Modifier | Settings |
| --- | --- |
| **Dead Zone** | Use **Radial** for vector length or **Axial** to test each component. Values at or below the lower threshold produce zero. Values at or above the upper threshold produce full output. The lower threshold must be below the upper threshold. |
| **Swap Axis** | Choose the source axis for output X or Y. Changing one field keeps the other source unique. |
| **Invert** | Reverse the sign of X, Y, or both. Scalar actions expose X. |
| **Scale** | Multiply X and Y by their factors. Scalar actions expose X. A factor of 1 preserves the input. |
| **Normalize** | Set a nonzero vector's length to 1 and preserve its direction. |
| **Response Curve** | Change how analog deflection responds. An exponent of 1 is linear. A larger exponent softens low input and a smaller one amplifies it. |

Modifiers run from top to bottom. Use **Add modifier** above the selected binding. The choices depend on the action's value type. Drag a modifier's title above or below another card to change the order. The up and down buttons also work with a keyboard. Use the trash icon to remove a modifier.

Older bindings with an authored contribution show the saved X and Y values. **Convert to modifiers** appears when the contribution can be represented exactly. Conversion keeps the binding ID and adds the equivalent transforms before the existing modifiers. A custom contribution that cannot be converted stays in place. Explicitly switching to a mouse axis that the old gain left at zero enables that axis.

## Review, undo, and save

Use **Undo** and **Redo** in the footer's **Map** menu, or press **Ctrl+Z** and **Ctrl+Shift+Z**. **Save** writes the map and updates the editor's cached resource as one Godot undo step. If you undo that step in Godot, the cached map changes immediately. Reopen it with **Edit Input Map** and save again to write the reverted contents to disk. If the asset changes outside the window while you are editing it, reopen the latest resource or save the draft as a separate copy.

Removing an action, binding, or modifier opens a confirmation. Choose **Cancel** to leave the draft unchanged. Choose **Remove** to remove it from the draft. Removing an action mapping keeps its separate action asset on disk.

Older layouts with inline actions still open in their compatibility editor. Adding a map to a layout does not rewrite or remove existing inline actions. Use the same gameplay and binding IDs when migrating an existing project so action reads and saved overrides still resolve.

For code-first setup and runtime customization, see [Input](../README.md) and [Rebinding](../rebinding/README.md).
