<!-- Copyright Francesco Di Blasi 2026 - All Rights Reserved -->

# Editor components

Open **Project → Tools → Editor Components** in the Godot editor to try the controls and their variants. Under **Components**, choose a control from the sidebar to see its live examples. Open **Design Style** for the layout and interaction rules behind the examples.

Use spacing for the relationship between things. Keep closely related elements together and put more space between distinct groups. The `xxs` through `xxl` spacing values in `editor_spacing.hpp` apply to any component. The bundled `design_style.json` supplies the usage examples in the native catalog and review gallery. [Carbon spacing foundations](https://www.carbondesignsystem.com/building-blocks/foundations/spacing/overview) · [Design Tokens format](https://www.designtokens.org/tr/2025.10/format/)

The catalog, Input Map editor, and Visual Script editor share a dark theme. The main work surface sits above quieter navigation and inspector surfaces. Borders separate regions and outline editable fields. The interface highlights hovered commands, selected items, and active tabs. Keyboard navigation and focus restoration work through Godot's controls. The theme follows Godot's editor scale and applies to Crimson tool windows and their popups. Menus, pickers, and tooltips have rounded frames when the window can composite transparent corners. On an opaque native host, their frames fill the window completely. Stock file dialogs follow the editor's window preference.

The bundled Inter 4.1 variable font supplies the standard UI face and a display optical size for larger titles. The addon ships the upstream SIL Open Font License with the font. [Inter 4.1 release](https://github.com/rsms/inter/releases/tag/v4.1)

The theme follows the dark desktop demo's 8-pixel control corners, 12-pixel popup corners, circular utility buttons, compact option rows, pill primary actions and switches, and 14-pixel checkbox frame. Searchable pickers use a slightly darker results area within the popup frame. Crimson's content tabs use Linear's Search documentation pattern with a one-pixel underline the width of the active label, a shared baseline, and 24 pixels between labels. Graph cards, typed sockets, numeric controls, and device bindings follow the same corner and state conventions as the other controls. Measurements are in logical pixels before applying Godot's editor scale. [Live Linear demo](https://linear.app/demo) · [Content tabs](https://linear.app/docs/search) · [Desktop refresh](https://linear.app/now/behind-the-latest-design-refresh) · [Typography and contrast](https://linear.app/now/how-we-redesigned-the-linear-ui)

## Build an editor tool

Add `editor_controls.hpp` and `editor_compositions.hpp` to the tool's C++ source. Set the theme on the tool window before creating its contents. Shared helpers live in `crimson::editor_ui`.

```cpp
window->set_theme(crimson::editor_ui::create_theme(window, editor_scale));
auto *search = crimson::editor_ui::search_input(toolbar, "Search assets", "Find an asset by name.");
```

The primitives are Button, Text Input, Text Area, Numeric Input, Slider, Checkbox, Switch, Radio, Select, Combobox, Disclosure, Tabs, Menu, Tooltip, and Progress. Use an icon button for an action with a short accessible name. Read-only text can receive keyboard focus for copying. Disabled controls reject focus and use a forbidden cursor. The numeric input uses minus and plus buttons and commits a bounded number. Use `select()` for indexed text choices, or `picker()` for previews and searchable groups. A compact Select matches the width of its trigger and shows the selected item with a trailing check. Moving through the list previews a choice before confirming it. Escape closes the list and returns to the trigger.

Standard Select uses 24-pixel rows and a compact frame. Pass `SelectStyle::MENU` when a value picker sits alongside a command menu and needs the same frame and row density, as with **Value type** and **Add Action** in Input Map. Both use a dark list fill and a one-pixel edge. Searchable pickers use a slightly lighter search header above that same list fill, leave room for complete result rows, and scroll for larger groups. Use the native command menu for commands, nested submenus, and keyboard accelerators.

Pass an existing `godot::Key` shortcut to `button()` or `icon_button()` to show it in the native tooltip as keycaps. The tool still owns the binding and the wording that explains what the command does. The catalog shows plain help and shortcut hints on its **Tooltip** page.

Use `drag_grip()` for ordered list rows, `show_drag_preview()` for a label beside the pointer, and an insertion rail to indicate the target boundary. Enclose the rail in the scroll region where the drop is valid. The owning tool validates the source, the target, and the resulting edit.

The compositions are Tree, Card, Field Group, Empty State, Item Row, Split Pane, Graph Node, and Dialog. Use `surface_panel()` to place content on a shell, primary work surface, or recessed sidebar. Give a Tree stable row IDs and detached content. A row can hold its own controls and trailing actions. Connect the selection callback to the tool's inspector state. Use `set_selected()` to restore selection without emitting another event. Each split pane owns its contents and uses the children's minimum sizes. To resize with a keyboard or controller, focus the divider and move in the pane's direction.

Use `editor_modal()` for a form or prompt inside an editor workbench. Put form controls in `body()` and use `open()` to name the task and choose its initial focus. A validator can keep the form open and show a specific correction with `set_validation_message()`. While the dialog is active, the tool checks `is_open()` before running its own keyboard shortcuts. Escape dismisses the prompt and returns focus when the previous control is still available.

Use `removal_confirmation()` when the prompt asks users to remove an authored item. It gives the Remove button destructive styling and initially focuses Cancel. The owning tool decides what removal changes.

Use `style_file_dialog()` after attaching a stock Godot file dialog to a toolkit window. Its square content frame fills the native window and its confirmation and cancellation buttons use the same dialog styles as the toolkit. Godot handles folder navigation, validation, and the footer layout.

Open **Graph Node** to see event and function cards with connected and free pins, a rounded variable getter, and a variable setter with a header gradient drawn from its value type. Include `editor_graph.hpp` and apply `style_graph_canvas()` to the tool theme and `GraphEdit` to use the same canvas and alignment guides. Add an `EditorGraphNode` to the canvas, configure the title role, and give it native Godot slot rows. Use port type 0 for execution and update `set_slot_connected()` when wires change. The graph tool owns the meaning and validation of its ports and connections. Custom graph widgets can use `style_graph_node()` with the optional value color and call `draw_graph_pin()` for the shared socket appearance.

Use stock Godot controls as the building blocks for a new tool. The tool supplies the input model, editable data, and undo history. The components own presentation and report user intent through callbacks. These classes are registered internally at editor initialization.

The catalog demonstrates Godot's native behavior and a scoped theme. Game interfaces can use their own presentation and input rules.

## Package an offline review

Run the review builder from the framework directory after capturing the staged editor. It packages the images and renders the same Design Style guide as the Godot catalog.

The **Design Style** tab shows spacing bars at token size, actual Inter roles, and native examples of control states and compositions. The bundled `design_style.json` supplies captions and specimen IDs for both the editor and the review gallery.

```powershell
python scripts/build_editor_review.py C:/Temp/crimson-review "Input Map=input:C:/Temp/input-captures"
```

Pass more groups to include the graph, catalog, and reference captures. Open `index.html` to review locally, or share the generated ZIP. Click an image to open the viewer. Use the arrow keys to browse and Escape to close it.
