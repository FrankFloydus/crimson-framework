<!-- Copyright Francesco Di Blasi 2026 - All Rights Reserved -->

# Visual scripting

Attach a Crimson Visual Script to a Godot node to run a graph when that node enters play. Use it to try scene behavior directly in the editor. Each scene instance runs the graph against its own node.

## Print a message when a scene starts

1. In Godot, create a scene with a **Node** root. Save the scene.
2. Select the root and choose **Attach Script**. Set **Language** to **Crimson Visual Script**, keep **Inherits** set to **Node**, and create the `.crimsonscript` file. Open the graph from the attached script icon. You can also select the script resource and click **Edit Visual Script** in the Inspector. [Attaching a script in Godot](https://docs.godotengine.org/en/stable/getting_started/step_by_step/scripting_first_script.html#creating-a-new-script)
3. Right-click an empty area of the graph. Add **On Ready** using search or the **Events** category. Right-click again and add **Print** from **Debug**.
4. Drag from the execution output on **On Ready** to the execution input on **Print**. Set **Message** to `Hello from Crimson`.
5. Use Godot’s **Scene → Save Scene** to save the attachment. Press **F6** to run the current scene. Look for `Hello from Crimson` in Godot’s **Output** panel. The message appears when the attached node becomes ready.

To attach an existing graph, look for the purple graph icon next to its `.crimsonscript` file in Godot's **FileSystem** dock. Drag it onto a compatible node in the **Scene** dock. For example, drop a script that inherits **MeshInstance3D** onto a mesh node. The purple icon appears beside the node in the Scene dock. Click it to open the graph. If the node is incompatible, an editor notification names the required base class. Save the scene to keep the attachment. Use **Ctrl+Z** to undo a drop and restore the previous script if needed.

To run a saved example in the framework project, open [ready_print.tscn](../project/tests/visual_scripting/ready_print.tscn) and press **F6**. It prints `Hello World`.

## Find your way around the graph editor

Open **Visual Script** from Godot’s main screen bar beside **Asset Store**. Click **2D**, **3D**, or **Script** to work elsewhere in the same editor. Click **Visual Script** again to return to the same graph. [Godot main screens](https://docs.godotengine.org/en/4.7/tutorials/plugins/editor/making_main_screen_plugins.html)

| Area | What you can do |
| --- | --- |
| Godot’s **Scene** dock | Drag a node from the current scene onto the graph canvas to reference it. |
| Script declarations, above **Details** in the right sidebar | Manage variables, enums, functions, macros, and custom events. |
| **Details** | Edit the selected declaration or graph node. Labels and fields reflow as you narrow the sidebar. Select a scene reference card to see its **Signals**. |

If several scene nodes share the same script, select its intended owner in Godot’s **Scene** dock before dragging the referenced node. The reference uses a path relative to the attachment.

With the graph active, use **Ctrl+Z** to undo or **Ctrl+Y** or **Ctrl+Shift+Z** to redo. Click empty canvas before using canvas shortcuts if you last worked in a dock or text field.

Crimson saves completed graph edits to the `.crimsonscript` file after a short pause. It also saves them when you open another script or run the game. Press **Ctrl+S** with the graph active to save immediately. Click **Apply** in the Custom Script editor to add your code changes to the graph. Save scene changes such as attaching the script or changing an Inspector override through Godot's **Scene → Save Scene**.

Use **Graph Tools → Reload from Disk** to read an external change to the script. The graph editor asks before discarding unsaved edits. If the graph cannot autosave when you open another script, choose **Save and Open**, **Discard Edits**, or **Cancel**.

On the canvas, hold **Shift** to add cards to the selection or **Ctrl** to remove them. The same modifiers apply when you drag a selection rectangle. While you drag a wire, a check beside the target shows that you can connect to that pin. Press **Escape** or switch to another main screen to cancel the unfinished drag.

Scene references and variable getters use the same compact card with a name and an output socket. A scene reference shows the scene node's name. Hover the name to see its class. Hover an input or output socket or its label to see the declared type. Selected nodes have a separate outline that stays visible as you zoom out. An unconnected resource field shows its asset filename.

Right-click empty canvas or press **Space** with the canvas focused to open the node picker. Click a node result once to place it, or press **Enter** to place the highlighted result. Click a disclosure arrow to expand a category. Drag from a pin to empty canvas to show compatible nodes and connect the new node automatically. Drag a compatible wire to an occupied input to replace its connection. Double-click a wire to insert a **Reroute**. Drag the dot itself to move it. Start a data wire just to its right to connect the same value to another input. For multiple execution paths, use **Sequence**. Select connected nodes and press **Q** to line up the connected sockets. If two wires require different heights, execution wires take priority. Right-click a selected node and open **Alignment** for card alignment, distribution, and grid snapping. Press **Delete** to remove selected nodes and their wires, or **Ctrl+Z** to restore them.

Type the full node name in the picker, including spaces. For example, search for `cast to character body` to narrow the Cast results.

The picker keeps the wire visible when you open it by dragging from a pin. Use the scrollbar to move through long result lists. Hover a socket or its label to see which pin the wire belongs to.

Execution pins highlight in gray when you hover their row near the arrow or label.

Edit an unwired input's default beside its pin label. For example, enter **First**, **Last**, and **Step** values directly on the three input rows of a **For** node. Connect a wire to use a value from the graph instead; disconnect it to edit the saved default again.

When you select a variable, **Instance Variable** opens in **Details**. Expand other sections, such as **Inspector** or **Signals**, when you need them. Click **Type** to browse the color coded categories or search for a Godot class such as `Node3D`.

## Edit and navigate a larger graph

Right-click a node to edit it. Right-click a node in a selection to edit the whole selection. The **Graph Tools** menu holds graph-wide commands such as **Paste Nodes**, **Find Nodes**, and the automatic wiring settings. Hover a data pin label to highlight the pin, then right-click it for actions such as **Promote to Variable**. The blank-canvas picker creates nodes, including **Add Custom Event** in the Event Graph.

| Goal | What to do |
| --- | --- |
| Reuse a group of nodes | **Ctrl+C** copies, **Ctrl+X** cuts, and **Ctrl+V** pastes at the pointer. **Ctrl+D** duplicates in place with an offset. The clipboard keeps relative positions and internal wires within the same script and graph. Event entries and graph boundaries stay in their original graph. |
| Duplicate with incoming data | **Alt+V** copies the selection and reuses its incoming data sources. Existing execution paths stay connected to the original nodes. |
| Follow the flow | **Alt+A** selects upstream nodes; **Alt+D** selects downstream. You can also double-click the left or right side of a node with the middle mouse button. |
| Fit the graph | **F** frames the selection, **Home** or **A** frames the whole graph, and **Alt+R** rearranges selected nodes. |
| Find a placed node | Type in the toolbar's **Find nodes…** field or focus it with **Ctrl+F**. Results from the current script appear just below the field and show their graph names. As you type, nonmatching cards on the current canvas dim. Click a result or use the arrow keys and **Enter** to jump to it. Press **Escape** to close the results and restore the canvas. |
| Find or open a declaration | **Ctrl+Shift+F** finds references to a selected declaration in the graph or right sidebar. Double-click a call node or use **Go to Definition** to open its declaration. When you open a function, Crimson frames its whole graph. |
| Return to a view | Click **Event Graph** in the breadcrumb to return from a function or macro. **Alt+Left** and **Alt+Right** go back and forward. **Ctrl+0–9** saves a Quick Jump and **Shift+0–9** recalls it within the same saved script. |
| Label a section | Select nodes and press **C**. Drag the comment frame to move it and its members together. Use the resize handle to change its size or the rename button to change its title. Deleting the frame leaves the nodes in the graph. |

To use a handbook example with **Copy graph**, open the Event Graph of a compatible Visual Script, click the canvas, and press **Ctrl+V**. The paste brings in the example's nodes, wires, and declarations in one undoable edit. For a declaration with the same name, choose **Replace** or **Keep Both**. **Keep Both** gives the imported declaration a free name such as `my_var_1` and updates the imported references. If the script already has **On Ready**, Crimson pastes the incoming entry as a Custom Event with its wires. Call that event explicitly to run its branch.

## Work with several wires

| Goal | What to do |
| --- | --- |
| Attach a data output to several inputs | **Shift-drag** from the output across compatible inputs. Release to connect them together. Or Shift-click the output, then click each input. Shift-clicking a connected input can pick up its source for reuse. Press **Escape** to leave this mode. **Graph Tools → Turn On Click Multi-Connect** also lets you start with a normal click. Pan and zoom while the output stays active. |
| Connect nearby nodes | Select the nodes and press **Alt+C** to connect unambiguous compatible sockets. A normal wire drag released over a node can choose its nearest free compatible socket. |
| Insert a node into a wire | Drag an existing node over a compatible wire. The canvas previews the two replacement wires as soon as the node can be inserted. Move away or hold **Alt** to see the original wire again, or release over the wire to apply the splice. Auto Insert starts on. If several pins can connect, Crimson uses the topmost available compatible pin on each side. An unconnected **Branch** uses **True**, and a **Sequence** starts with **Then 1**. Existing input and execution wires keep their pins. Use **Graph Tools → Turn Off Auto Insert** to disable it. To create a new node on a wire, right-click the wire and choose **Insert Node on Wire**. |
| Preview automatic connections | Enable **Graph Tools → Turn On Auto Connect** and move a node near a compatible neighbor. Release to accept the preview. Hold **Alt** to suppress automatic wiring during a move. Auto Connect starts off. |
| Remove or bypass a node in a chain | **Alt+X** removes the selection and reconnects a compatible chain. **Alt+Shift+X** keeps the bypassed nodes. Use **Bypass and Copy** to put removed nodes on the clipboard. To shake a node loose, hold the left mouse button on the card and quickly shake it back and forth. The canvas shows the reconnected route as soon as it detects the shake. Release to apply the change. Start the shake on any selected card to bypass the entire selection. The cards stay on the canvas, their internal wires stay attached, and each independent path reconnects to its own neighbors. Nodes such as **Branch** and **Sequence** can be shaken loose when they have one connected execution route through them. Ambiguous paths and execution chains with required external data outputs are left intact. One Undo restores the move and the wires. |
| Exchange wires | Click one wire and Shift-click another, then press **Alt+T** to exchange compatible destinations. You can also use **Alt+T** on two pins of the same direction, one after the other. |
| Inspect or cut several wires | Click a wire to highlight it. **Shift-click** adds another wire and **Ctrl-click** removes a wire from the highlight. Double-click a pin with the middle button to highlight its links. **Alt+middle-drag** across wires cuts them in one edit. |
| Clear a connection or override | **Alt-click** a socket or use **Break All Pin Links** in its menu. Use **Break All Node Links** for selected nodes, **Dissolve Reroute** to preserve a reroute's connections, or **Reset Input to Default** on a data input. |
| Change how wires draw | Use **Graph Tools → Cycle Wire Style** for curved, straight, or right-angle lines. **Use Neutral Wire Colors** quiets data wires while sockets keep their type colors. |

## Create a node from the graph

Right-click the graph and browse **Create Node**. The picker follows Godot’s native node class hierarchy. Open the **Node3D** branch to find 3D classes, or search for **MeshInstance3D** by name. The picker expands its parent classes so you can see where the result lives. Non-instantiable classes appear as categories.

Create a separate **Node** scene and attach a new Crimson Visual Script.

1. Add **On Ready**. Click **MeshInstance3D** in **Create Node**. Reopen the picker and add **Add Child** from **Scene**.
2. Connect the execution output on **On Ready** to the execution input on **Create MeshInstance3D**, then connect the execution output on **Create MeshInstance3D** to the execution input on **Add Child**.
3. Connect the constructor’s **MeshInstance3D** output to **Add Child → Node**. Leave **Parent** empty to attach the child to the node running the script.
4. Save the scene, then press **F6**. Switch the Scene dock to **Remote** and expand your scene root to see the new child. A fresh MeshInstance3D has an empty mesh, so the Remote tree shows the result.

## Play a sound when a scene starts

Create a scene with a **Node3D** root and a **Camera3D** child. Enable **Current** on the camera and set its **Transform → Position → Z** to `2`. The camera acts as the listener two meters from the sound. Copy a short `.wav`, `.ogg`, or `.mp3` file into the project and wait for Godot to import it. Attach a new **Crimson Visual Script** to the root. [Godot audio import](https://docs.godotengine.org/en/4.7/tutorials/assets_pipeline/importing_audio_samples.html) · [AudioStreamPlayer3D](https://docs.godotengine.org/en/4.7/classes/class_audiostreamplayer3d.html)

1. Right-click the graph and add **On Ready**, **AudioStreamPlayer3D** under **Create Node**, and **Add Child** under **Scene**.
2. Connect the execution output on **On Ready** to the execution input on **Create AudioStreamPlayer3D**. Connect the constructor's execution output to the execution input on **Add Child**, and its **AudioStreamPlayer3D** output to **Add Child → Node**. Leave **Parent** empty to attach the player to the scene root.
3. Drag the constructor's **AudioStreamPlayer3D** output to empty canvas and release. Choose **Set Stream**. The picker connects the player's output to **Target**. Use **Load** or **Quick Load** beside **Value** to select the imported sound, or drag it onto the field from **FileSystem**.
4. Drag the constructor's **AudioStreamPlayer3D** output to empty canvas again and add **Play** from the same list. Leave **From Position** at zero to play from the beginning.
5. Connect the execution output on **Add Child** to the execution input on **Set Stream** and the execution output on **Set Stream** to the execution input on **Play**. The execution wires put the player into the scene, assign its audio stream, and start playback in that order.
6. Save the scene, then press **F6**. You should hear the sound once. In the **Remote** scene tree, expand the root to see the new **AudioStreamPlayer3D**.

To try a saved graph in the framework project, run [audio_members.tscn](../project/tests/visual_scripting/audio_members.tscn) with **F6**. It creates an **AudioStreamPlayer3D**, plays a short chime, and prints `CRIMSON_VISUAL_SCRIPT_AUDIO_READY`. The constructor's output supplies the **Target** pins. The graph also reads **Volume dB** and passes it into **Set Max dB** to show a typed **Get** output.

## Read properties and call methods

Drag from a typed output to find methods for that value or object. A `String` pin offers **Begins With**, **Ends With**, and **Replace**. A `Vector3` pin offers **Length** and **Normalized**. Choose a method to connect its **Target** automatically, then connect execution to run the call and use its result. To find a specific engine name, search with the method name or a qualified name such as `JSON.stringify`.

Open the picker on empty canvas to browse **Global Functions**, **Native Static Methods**, and **Built-in Types**. A global function such as `clampf` or a static method such as `JSON.stringify` has no Target. Methods with a variable number of arguments have fixed pins for their declared parameters and an **Additional Arguments** Array for the rest.

Search for `KEY_ESCAPE` to insert a named keycode into the graph. You can also browse **Global Constants → Key**. For example, compare an `InputEventKey`'s **Get Keycode** result with `KEY_ESCAPE` using **Equal**, then wire the Boolean result to a **Branch**. Global constant nodes have one read-only `int` output. They also appear when you drag a wire from a compatible input. [Godot global constants](https://docs.godotengine.org/en/4.7/classes/class_%40globalscope.html)

Search for `Input`, `Engine`, or a project Autoload by its registered name. You can also browse **Singletons → Engine** or **Singletons → Autoloads**. Drag from the result to call its typed methods or read its properties. For example, add an Autoload named `GameState` under **Project Settings → Globals → Autoload**, then search for `GameState` and drag from its output to call the project's current score method. A service registered through `Engine.register_singleton()` appears in the picker while it is registered in the editor and must be available before the running graph reads it. [Godot Autoloads](https://docs.godotengine.org/en/4.7/tutorials/scripting/singletons_autoload.html)

Use **Cast To** followed by a node or script type to test an object before using it. Connect the object to **Object** and handle **Success** and **Failure**. For example, cast the value from a trigger's body signal to `CharacterBody3D` before changing its velocity. Use the typed result from **Success** to call the character's methods.

Use **Input Actions** to read actions from Godot's **Project Settings → Input Map**. For example, select your `interact` action in **Is Action Just Pressed** and wire its result to a Branch evaluated by **On Process**. Use **Get Axis** for opposing actions and **Get Vector** for four direction actions.

Use **Make Vector3**, **Make Color**, and other **Make** nodes to build a value from its fields. Leave a field unwired to use the default shown on the node. A **Break** node exposes the fields individually. For example, wire a position into **Break Vector3** and check only its **Y** output.

Under **Resources**, use **Preload Resource** to select a saved project asset at authoring time. Open the asset menu and choose **Browse Files** or drag the asset in from **FileSystem**. You can choose text or binary scenes, scripts, imported textures and audio, materials, and other assets Godot can load as resources. A scene is a `PackedScene`, ready to wire into **Instantiate**. The graph prepares the resource before running and reports it as an export dependency. The card shows a thumbnail when Godot provides one and a type icon otherwise. Save a scene in Godot to generate its thumbnail. [Godot preload](https://docs.godotengine.org/en/4.7/classes/class_%40gdscript.html#class-gdscript-method-preload)

You can also drag an asset from Godot's **FileSystem** dock onto empty canvas to create a typed **Preload Resource** node. For example, drop a sound asset and wire its output to **Set Stream → Value**. When you drop a saved scene, choose **Preload Resource** to use it as a `PackedScene`, or **Instantiate** to create a node from it when the graph runs. To put that instance in the running scene, connect it to **Add Child**.

Use **Load Resource** when the graph chooses the path at runtime. For example, wire a selected skin's path to **Path**, run the node when the player selects the skin, and use **Resource** on the **Success** branch. Connect **Failure** to show a fallback when the file is missing or has the wrong type. To get a typed result, drag from a typed resource input and add **Load Resource** from that pin's picker. Loading is synchronous. If your export includes selected resources only, include any files the graph will choose by path. Use **Get Resource Path** to read a loaded resource's path. [Godot ResourceLoader](https://docs.godotengine.org/en/4.7/classes/class_resourceloader.html#class-resourceloader-method-load)

Drag a saved `.gd` file onto empty canvas and click **Create from Script** in the menu at the drop point. Press **Escape** or click outside the menu to cancel. You can also search for **Create from Script** in the node picker and browse for a file. The card shows the GDScript constructor's typed `_init` inputs and returns **Instance** with the script's type. For example, set the amount on a scripted pickup, wire **Instance** and execution to **Add Child**, then drag from **Instance** to find the pickup's public members. This command supports GDScripts with a concrete `Node` or `RefCounted` base. [GDScript constructor](https://docs.godotengine.org/en/4.7/classes/class_gdscript.html#class-gdscript-method-new)

Drag a scripted child from Godot’s **Scene** dock into the graph, then drag from its reference pin. For example, a door controller can offer **Unlock** and **On Opened** directly, with its native Godot actions grouped by class. For a reusable variable or function pin, select the script’s `class_name` under **Script References** in the type picker.

Drag a typed object output to empty canvas. The picker lists the type's own actions first, with no category to open. For example, a **Camera3D** pin shows **Get Fov**, **Set Fov**, and **Project Ray Origin** directly. Expand **Node3D** for **Get Position** or **Node** for **Get Parent**. Inherited classes start collapsed, and their getters, setters, methods, and signals are listed together. **Context Sensitive** starts enabled. Turn it off to browse other actions while keeping your search. If a selected action has no compatible pin, it is placed without a connection. Hover a native method or event to read its description when documentation is available.

To get the node that runs the graph, add **Scene → Self**. It appears as a compact **Self** card. Its output has the script's base type, so you can wire it to another node or drag from it to browse members. To read a property from the node running the script, right-click empty canvas and choose a getter from a compatible class. For example, **Get Visible** on a Node3D script appears as **Visible** with a single Boolean output. Drag from another object's output and choose the same getter to use that object's property. The new node shows its wired **Target** input.

To retarget an existing getter, right-click it and choose **Show Target**, then connect the object you want to read. For an unwired getter that can use the script's node, choose **Hide Target** to return to the compact view. Property setters and method nodes show their target. An unwired **Target (Self)** uses the node running the script.

- A **Get** node produces a typed value when another node reads its output. For example, wire **Get Stream** from one player to **Set Stream → Value** on a second player to share the same sound.
- A **Set** node writes the property when execution reaches its execution input. Set **Value** on the node or connect a compatible data output to **Value**. Disconnect the wire to edit the saved value again.
- A method node uses Godot's argument names, types, and defaults. Connect its execution input to run it. When the method returns a value, connect **Result** to a node that runs after the call.

For a resource input such as **Set Stream → Value**, use a standalone asset saved in the project. The picker accepts the type declared by the pin, including compatible subclasses such as **AudioStreamWAV** for **AudioStream**.

## Choose which nodes run

Open **Flow Control** in the picker to choose which actions run and in what order. Connect the right execution arrow of one node to the left arrow of the next. Named branches such as **True** and **False** keep their labels. Rounded data pins carry typed values. Connected pins are filled, and free pins are outlined.

In the **Event Graph**, connect **Delay** to continue after a number of seconds. For example, delay after displaying a pickup message, then hide it. Use **Await Signal** to resume on the next emission. Drag from a typed object reference and search for **Await** plus its signal name to get arguments on typed output pins. For example, await an `AnimationPlayer`'s **Animation Finished** after starting it. Execution resumes along **Then**, where you can pause or step in the graph debugger. Waits stop when the graph owner leaves the tree or loses its script. Use waits in Event Graph paths and Sequences. Function and macro bodies and loop iterations run synchronously.

| Node | Use it to |
| --- | --- |
| **Branch** | Read a Boolean **Condition** and run either **True** or **False**. For an else-if check, connect **False** to a second **Branch**. |
| **Switch** | Compare **Value** with the configured cases and run the matching output. **Default** handles values with no match. |
| **Switch on Enum** | Branch on an enum defined in the script or by a Godot class. Drag from a typed enum output or search for **Switch on Enum** in the picker. Each distinct value has a named output. **Default** handles unknown values. |
| **Sequence** | Run the **Then** outputs in their displayed order. Each path runs before the next one starts. |
| **While** | Read **Condition** before each pass. Run **Body** while it is true. |
| **For** | Run **Body** with **Index** starting at **First** and stepping toward **Last**. **Last** is inclusive if reached, and **Step** can be negative. |
| **For Each** | Run **Body** once per Array item or Dictionary pair. **Index** is the zero-based position, **Item** is the value, and a known Dictionary also exposes **Key**. |
| **Break** and **Continue** | Exit the current loop or move to its next iteration. |
| **Do Once** and **Do N** | Let execution through once or up to **Count** times. Use **Reset** to start over. |
| **Flip Flop** | Alternate between outputs **A** and **B**. **Is A** reports which output ran. |
| **Gate** | Let execution through the execution input while open. Change the state through **Open**, **Close**, or **Toggle**. |

Use **Add Pin** on a **Sequence** card to add another output. Right-click an output and choose **Remove Pin** to remove that branch and its wire. Sequence supports 1 to 64 outputs. Select a **Switch** to enter its **Cases** in **Details**. Use a loop's **Completed** output for work that should happen after the loop. Keep loop work short because the graph runs it in the current call. Stateful nodes such as **Gate** keep their state on the current script instance between calls.

Connect a collection to **For Each**. When the editor knows its types, the input socket shows the element color or separate dictionary key and value colors. **Item** shows the element or value type, and **Key** shows the dictionary key type. For example, drag **Item** from a loop over an **Array of PackedScene** into the picker and choose **Instantiate**. This also works when **Make Array** supplies the scenes through a reroute. Replacing the source updates the pins. If the new types make loop output wires incompatible, the editor removes those wires as part of the same change. Undo restores the source and its wires.

Use **Index**, **Item**, **Key**, and values produced by loop actions inside **Body**. If you need a result after **Completed**, save it to a variable inside the loop. An action’s output from before the loop is restored after each pass. Variable writes and state changes carry forward.

## Store a setting and call a function

Create a **Node** scene named `TrainingRoom` and attach a **Crimson Visual Script**. Make it print a startup message only when a Boolean setting is enabled.

1. In the right sidebar, click **+** beside **Variables**. In **Details**, set **Name** to `show_message` and press **Enter**. Keep **Type** set to **bool** and check **Enabled** under **Default Value**. Each node with this script gets its own value.
2. Click **+** next to **Functions**. The function graph opens with **Function Entry** wired to **Return**. Set **Name** to `announce` and press **Enter**. Expand **Inputs** in **Details** and click **+ Input**. In the new input card, set **Name** to `Message`, press **Enter**, and choose **String** for **Type**.
3. Add **Print** in the function graph. Connect the execution output on **Function Entry** to the execution input on **Print**, and **Function Entry → Message** to **Print → Message**. Connect the execution output on **Print** to the execution input on **Return**. The new wire replaces the original wire into **Return**.
4. Click **Event Graph** in the breadcrumb and add **On Ready**, **Branch**, **Get show_message**, and the `announce` call. The variable and function are searchable by name. You can also drag their entries from **Variables** and **Functions** onto the canvas.
5. Connect the execution output on **On Ready** to the execution input on **Branch**, **Get show_message → Value** to **Branch → Condition**, and **Branch → True** to the function call’s execution input. Enter `The training room is ready.` in the call's **Message** field. Leave **False** unconnected.
6. Save the scene, then press **F6**. **Output** shows `The training room is ready.` Clear **Enabled** under the variable's **Default Value** and run again to check the other path.

Drag an entry from **Variables** onto the canvas to place its getter. Hold **Ctrl** when you drop it to place the setter. A badge by the pointer identifies the variable, function, or scene node you are dragging. The symbol after the variable name matches its type in **Details**. A green check appears when the getter or setter can connect to the hovered pin or pin label. Select a getter or setter on the canvas to edit the same variable in **Details**. **Local Variables** appears in the right sidebar when you open a function graph. Its variables start from their defaults each time the function runs.

To change the order in the right sidebar, drag a declaration above or below another entry in the same section. Variables, local variables, functions, macros, custom events, and enum declarations each have their own section. Existing references follow the declaration when it moves.

Right-click a data pin and choose **Promote to Variable**. You can also drag the pin to empty canvas and choose the same action from the picker. In a function graph, choose **Promote to Local Variable** to keep the value in that function. Promoting an input creates a connected getter. Promoting an output creates a connected setter. Wire the setter’s execution input to assign the value during execution.

Drop a variable onto a compatible data input to add and connect its getter. Hold **Ctrl** and drop it onto a compatible data output to add a setter and connect its value input. Undo removes the node and its connection together. Dropping on an incompatible pin leaves the graph as it was.

To organize declarations, right-click an entry under **Variables**, **Functions**, or **Custom Events**. The menu offers **Rename**, **Cut**, **Copy**, **Duplicate**, and **Delete**. Press **Delete** with the row focused to remove it. Cut and Delete remove the declaration, its graph references, and their wires in one undo step. Paste a copied or cut declaration within the same script. Pasting a function brings its body and signature. Set **Description** in **Details** to explain what the declaration does when someone hovers it.

## Give each scene node its own setting

Export a graph variable when different nodes should share the same script but start with different values. For example, two health pickups can use the same graph and restore different amounts of health.

1. Under **Variables**, add an `int` variable named `heal_amount` and set its default to `25`. Click the eye beside the variable, or enable **Show in Inspector** under **Export** in **Details**.
2. Attach that script to both pickup nodes. Select the large pickup in Godot's **Scene** dock and set **Heal Amount** to `50` in the Inspector. Leave the small pickup at its default. Save the scene.
3. In the graph, connect **Get heal_amount** to your heal logic. The small pickup reads `25`. The large pickup reads `50`. The ordinary **Set heal_amount** node changes the value for the running pickup.
4. Change the graph default to `30`. The small pickup now reads `30`; the large pickup still reads `50`. Use the Inspector's revert arrow beside **Heal Amount** on the large pickup and save the scene when you want it to follow the script default again.

You can also export typed references. For an `AnimationPlayer` variable, pick a node in the scene. For an `AudioStream` variable, choose a resource from the project. Leave the reference empty in the shared graph when each scene node supplies its own. Export applies to variables on the script instance; local variables still belong to a single function call.

For a weapon's exported `float` variable `reload_time`, expand **Inspector** in the variable's **Details**, then set **Group** to `Weapon Tuning` and **Units** to `s`. Press **Enter** after each text field. Enable **Use Range**, set **Minimum** to `0.1`, **Maximum** to `5`, and **Step** to `0.1`, and press **Enter** to apply the three values. Enable **Show Slider** and select the weapon node in the Scene dock to see **Reload Time** under **Weapon Tuning**. A range sets the slider interval and editing step. You can type a value outside it. **Units** can also be used on its own. Give multiple exports the same **Group** to display them together. Changing these settings preserves values saved in scenes.

The variable name is the name Godot saves in the scene. Crimson asks before renaming, removing, or hiding an exported variable because open scenes can lose overrides for the old property. Graph Undo restores the declaration and its graph references. Record any scene overrides you want to keep before confirming.

Choose a name that does not match a built-in property on any node using the graph. For example, use `pickup_offset` instead of `position` when the graph is attached to a `Node3D`. If you attach a shared graph to a node with a conflicting property, Crimson hides that export and warns you to rename it. The graph can still use the variable internally.

## Write a Custom Script node

Use **Custom Script** when a few lines of GDScript express a calculation or a short sequence more clearly than several graph cards. The code runs when execution reaches the left arrow. When the body finishes successfully, its outputs become available and execution continues through the right arrow. Select the card and edit **Name** in **Details** to describe what it does. Press **Enter** to apply the name or clear it to restore **Custom Script**. The code badge at the upper right identifies the card after you rename it.

For example, use a node to subtract rounds when a weapon fires and send the remaining count to the HUD.

1. Under **Variables**, create an integer variable named `ammo` and set its default to `30`. Under **Custom Events**, add `magazine_changed` with an integer input named `remaining`.
2. Add **Custom Script** from the node picker. Expand **Inputs** and **Outputs** in **Details**. Add an integer input named `rounds` and an integer output named `remaining`. Use **Edit Code** to enter the following body.

```gdscript
remaining = maxi(graph.ammo - rounds, 0)
graph.ammo = remaining
graph.magazine_changed(remaining)
```

3. Click **Apply** to add the code to the graph. Wire the action that consumes ammo into the left execution arrow and supply the number of rounds through **rounds**. Read the **remaining** output after the script runs. With `ammo` initially set to `30`, sending `3` produces `27` and passes that count to the event.

Each input is available by name. Assign to outputs by name. An unassigned output has its GDScript type's default value. Use `owner` for the node that owns the visual script and `graph` for its variables, functions, and custom events. When calling a graph function or event, supply all of its declared arguments. Type a dot or press **Ctrl+Space** for suggestions from Godot's GDScript language server. The generated context gives it the declared types and reports diagnostics against the visible body. [Godot language server](https://docs.godotengine.org/en/4.7/tutorials/editor/external_editor.html#lsp-dap-support)

You can define a named `func` at the left margin and call it from the node's body. The helper can use `owner` and `graph`. Pass pin values to it as arguments and use its result to assign an output. For example, `remaining = subtract_rounds(graph.ammo, rounds)` can call a helper you declare in the same CodeEdit panel.

A Custom Script body runs synchronously. Assign outputs in the body, and use `return` inside helper functions. Split work that needs `await` into separate graph steps. If the editor's language server is unavailable, you can still edit and apply the body. Game builds do not need the language server. Use typed resource inputs for assets that must be included in an export that selects specific scenes or resources. The graph debugger can break on the node.

Pin names must be valid and unique GDScript identifiers. Renaming a pin created in **Details** preserves its wires. Update its name in **Edit Code** as well. To remove a pin, disconnect any wires that use it. If you rename a graph member or move a node to a graph with different local variables, update any affected names in the body.

### Declare pins in code

For the same ammo node, you can keep the `rounds` input in **Details** and add a `remaining` output from **Edit Code**. Before doing this, remove the existing `remaining` pin from **Details** so the name is available. Try the following body.

```gdscript
#@input
var minimum_ammo: int = 0

#@output
var remaining: int = maxi(graph.ammo - rounds, minimum_ammo)

graph.ammo = remaining
graph.magazine_changed(remaining)
```

Put each directive on its own line at the left margin. On the next line, write the complete `var` declaration with an explicit type. Use a built-in type like `int`, `Vector3`, or `Array`, or a native Godot class like `AudioStream`. A plain `var` creates a local variable. **Ctrl+Space** suggests both kinds of pin as you edit. Click **Apply** to add the declared pins to the card. Select the card to see their names and types in **Details**, and use **Edit Code** to change them.

Crimson uses the input initializer when the pin is unwired. Without an initializer, an unwired input uses its GDScript type's default value. A wire supplies the value when connected. GDScript evaluates the output initializer when it reaches the declaration, and you can assign a new value later in the body. Changing an initializer keeps the pin's wires. If a rename, removal, or type change would break a wire, disconnect that wire before applying the edit.

## Edit collection defaults and enums

Use **Make Array** to gather values in order or **Make Dictionary** to look up values by key. For example, add **Make Dictionary** from **Collections** and enter `wood` in **Key 1**. Connect an integer output to **Value 1** to store a count. When you choose **Add Pin**, the new key takes **String** and the new value takes **int**. You can establish either type first. Each type applies to that side of the card, and each side uses its own color in the output icon. Fill both pins to include the pair in the result.

Removing an entry keeps its established key and value types available for the next pair. If the editor reports a conflict with a wire connected to the Dictionary output, disconnect that wire before setting the new type.

The **Make Array** icon uses the item color. Unknown types and mixed Array items use gray in the output icon.

Choose **Add Pin** to add another item or key-value pair, up to 32 per card. Right-click an input pin and choose **Remove Pin**. On a Dictionary, either pin removes the whole pair. Undo restores the removed entry and its connections.

**Get Array Item** and **Get Dictionary Value** expose a **Found** output so you can branch before using a missing value. Collection edits return a changed copy and a **Succeeded** result. Wire the new collection into its ordinary variable setter when the change should persist.

In variable **Details**, use **Container** to switch between a single value, an Array, and a Dictionary. When you make an Array, its element type starts with the previous variable type. When you make a Dictionary, its key starts with the previous type if that type supports persistent keys. Set the Dictionary's **Value Type** separately. Use **Variant** for a collection side that can hold several types.

Under **Variables**, a typed Array shows its element type beside an Array icon in that type's color. A Dictionary shows its key type beside a two-color icon. The small squares use the key color, and the large squares use the value color. Hover the variable to see the full collection type.

The confirmation tells you that changing the type resets the default. It also lists how many incompatible wires and setter values the edit will clear. Confirm to update all getter and setter nodes and keep their compatible wires. Reconnect any required input with the new type. Use **Ctrl+Z** to restore the previous type, default, and wires together.
For an **Array** default in **Details**, choose **Add Item**. Choose the item's type, edit its value, and press **Enter** to apply that field. For a **Dictionary**, choose **Add Pair** and set a unique key and its value. Adding, removing, or reordering entries keeps pending edits in surviving fields. Undo and Redo restore the saved values and order while keeping drafts on their original rows in the open Details panel. Drag a grip to reorder rows, or focus it and press **Alt+Up** or **Alt+Down**. Array order also determines the order in the running game. Dictionary row order is saved for editing. A function or macro input can also have a collection default. Use **Clear default** if callers should supply the value themselves.

For a `PackedScene` default, use the resource field to pick a saved scene or drag one from Godot's **FileSystem** dock. The same field appears on each item in an **Array of PackedScene** and each value in a Dictionary whose value type is `PackedScene`. An empty field means `null`. For example, fill an array with pickup scenes, connect its getter to **For Each**, and use the typed **Item** output as the **Instantiate** target. Saved asset defaults are detected when Godot exports only selected resources. Runtime objects such as a `Node` reference must be supplied by the graph.

Click **+** beside **Enums** to define a list of named choices, such as `Locked` and `Open`. Name the enum in **Details**, then expand **Members** to add and name its values. Drag a member by its grip or focus it and press **Alt+Up** or **Alt+Down** to change its position. Reordering keeps each numeric value and its connected switch wires. Choose the enum from **Type → Enums** for a variable or signature pin. Drag from a pin of that type and select **Switch on** followed by the enum name. Connect an execution path to each case you want to handle, and use **Default** for other values. These enums belong to the current visual script. An unwired enum input on a node offers the same named choices.

An entry under **Enums** defines the named values. An entry under **Variables** holds one of those values for the current script instance. Select the enum and choose **Create Variable** in **Details**, then choose the variable's **Default** value. You can also assign the enum's type to an existing variable from its **Type** picker. Drag the variable from **Variables** onto the matching switch's **Selection** pin to add and connect its getter in one edit.

## Reuse a group of nodes

Select the nodes to move, then right-click one of them. Choose **Collapse to Function** to make a callable graph, or **Collapse to Macro** to make a reusable graph with configurable execution entrances and exits. The editor replaces the selection with a call. A value producer used only by the extracted nodes moves with them if it has no execution pins and can run inside the new graph. Shared values and values tied to the caller stay outside, connected through typed inputs. Double-click the new entry under **Functions** or **Macros** to open its graph and edit its signature in **Details**.

Select a function or macro call in the graph to edit its declaration in **Details**. **Description** appears when you hover the call, **Category** groups it in the picker, **Keywords** help you search for it, and **Compact Title** changes the card label. Edit **Inputs** and **Outputs** here or select the entry under **Functions** or **Macros**. Both places edit the same signature, so changes apply to every call. The editor rejects incompatible signature changes that would break existing wires.

Include the loop when extracting **Break** or **Continue**. Select the full connected section if a path leaves the selection and returns to it. When a macro has several execution exits, extract those routes first and add any shared data outputs in **Details**. Supply the values for those outputs on each exit.

## Pause and inspect a running graph

1. Open a saved graph with **On Ready** connected to **Print**. Right-click **Print** and choose **Add Breakpoint**, or select it and press **F9**. A red marker appears.
2. Run the scene from Godot with **F6**. The graph opens at **Print** with a **Paused** badge. The message has yet to appear in **Output**.
3. Look in **Debugger** below **Details**. The heading tells you which node is about to run and which scene instance is running it. **Inputs** shows the values about to be used. Hover a value to see where it came from or, for an object, its full scene path. **Computed** shows results used by this step. Drag the divider above **Debugger** to give it more space.

| Button | Shortcut | What it does |
| --- | --- | --- |
| **Continue** | **F5** | Run until another breakpoint or the scene ends. |
| **Step Over** | **F10** | Run the current node and pause at the next node in this graph or its caller. |
| **Step In** | **F11** | Pause at the next node, entering a function or macro if the current node calls one. |
| **Step Out** | **Shift+F11** | Finish the current graph call and pause at its caller's next node, if there is one. |

The toolbar appears during an editor debug session and enables these controls when a graph is paused. A breakpoint pauses before the node runs. Inspection never re-runs a node, and large values such as collections are summarized. Breakpoints are saved with your local editor project. Release exports do not pause at them.

### Watch a pin

Hover a data pin while paused to see its current or last captured value and how the graph got it. Right-click the pin and choose **Watch Pin** to keep it in **Watches** while you step. Use **Remove Watch** in the pin menu or the remove button in the debugger.

A watch can show **Not evaluated** before the node produces a value. That is different from `null`, which is a recorded value. A pin on a suspended caller can still show its last value. When that invocation exits the call stack, the value is no longer available. Watch selections are saved with your local editor project; captured values clear when the game resumes or stops.

## Bypass an action while testing

Right-click an action with one execution input and one execution output and choose **Disable (Do Not Compile)**. A diagonal pattern covers the card, and its wires stay connected. When execution reaches the action, it skips directly to the next node. Its data outputs behave like unplugged inputs at their destinations. The disabled action stays skipped when you save or export. Choose **Enable Compile** to restore it.

## React to an event

Open the **Event Graph** and use **Events** in the node picker for Godot lifecycle callbacks. **On Ready** runs when the node becomes ready. **On Process** and **On Physics Process** supply **Delta** in seconds. Input callbacks expose the Godot input event as a typed pin.

1. Create a **Control** scene with a **Button** child. Set the button's **Text** to `Start training` and attach a visual script to the root.
2. Open the **Event Graph** and drag the button from Godot's **Scene** dock onto the canvas. Select its reference card and expand **Signals** in **Details**. Click **+** beside **On Pressed**. Hover the row to read what the signal does.
3. Connect the execution output on **On Pressed** to the execution input on **Print** and enter `Training started.` in **Message**. Save the scene, press **F6**, and click **Start training**. The message appears in **Output** each time the button is pressed.

You can add more than one event node for the same signal. Each event has its own connection and can drive a different path.

In the **Event Graph**, you can also drag a typed object output to empty canvas and choose a signal action from the same list as its methods and properties. For example, drag from a **Button** output and choose **BaseButton → On Pressed**. The picker wires **Target** for you. **Sender** supplies the emitting object and any signal arguments appear as typed outputs. When the graph creates the source node, it starts listening when the constructor produces the object, before the graph adds it to the scene.

In the **Event Graph**, right-click blank canvas and choose **Add Custom Event** to create an entry and open **Details**. Set the event name and add typed inputs there. Open the graph where the event should fire, select the entry under **Custom Events**, and click **Add Emit Event** in **Details**. The event entry's output pins carry the supplied arguments.

## Call a visual script from C++

An authored function or custom event is callable on the Godot node that owns the script. Use the declared name and pass arguments in the order shown in **Details**. For example, from a GDExtension with a pointer to the `TrainingRoom` node in the example scene below, `training_room->call("announce", "The next drill is ready.");` runs the graph and writes the new message to **Output**. Use `has_method("announce")` before calling when the node may have a different script. [Godot Object.call](https://docs.godotengine.org/en/4.7/classes/class_object.html#class-object-method-call)

To call C++ from a graph, register your GDExtension class and bind public instance methods with `ClassDB::bind_method`. Expose a property with `ADD_PROPERTY` or a typed signal with `ADD_SIGNAL`. Rebuild the extension and use its class in the scene. The graph picker reads the Godot declarations and shows their compatible pins. [Godot GDExtension methods and signals](https://docs.godotengine.org/en/4.7/tutorials/scripting/cpp/gdextension_cpp_example.html)

## Run the visual scripting checks

To regenerate native tooltip descriptions, run Godot 4.7 stable with `--headless --dump-extension-api-with-docs` in a temporary directory. From `framework/`, run `python scripts/generate_visual_script_help.py <path-to-extension_api.json>`. The generator includes descriptions from Crimson's class XML and writes the editor's bundled index.

Open [workbench_example.tscn](../project/tests/visual_scripting/workbench_example.tscn) in the framework project and press **F6**. It reads a Boolean variable, follows the **True** branch, and calls an authored function. Godot's **Output** panel shows `The training room is ready.`

To run the automated checks with both installed Windows DLLs, open PowerShell 7 at the workspace root. Pass the path to your Godot 4.7 console executable.

```powershell
.\framework\project\tests\visual_scripting\Test-VisualScript.ps1 -GodotExe 'C:\Path\To\Godot_v4.7-stable_win64_console.exe'
```

The harness imports the addon and its own fixtures in isolated projects. It prints a pass marker for each build and records the Godot output in the temporary folder it reports. Add `-VerifyPack` to check a selective export with the release DLL. The pack must include an audio asset referenced by a Custom Script input and a scene referenced only by an Array default. Use `-VerifyEditor` to exercise interrupted wiring, repeated collection reorders, rejected values, save and reopen, Custom Script completion and diagnostics, and the live debugger. Use `-VerifyDebugger` for live pause, inspect, step, and continue checks alone.

On an unlocked Windows desktop, add `-VerifyNativeFocus` to exercise focus loss between the Godot editor and a separate test window. The test interrupts a wire drag in each direction, returns to the **Visual Script** screen, and connects the pins with a fresh drag.

To compare loop performance, run the separate benchmark with the installed Windows release DLL. It runs the same integer arithmetic, branch, and rolling checksum in [GDScript](../project/tests/visual_scripting/loop_benchmark.gd) and in a [Visual Script graph](../project/tests/visual_scripting/loop_benchmark.crimsonscript). To see the workload in the graph editor, open the script and select **run_batch** under **Functions**.

```powershell
.\framework\project\tests\visual_scripting\Test-VisualScriptPerformance.ps1 -GodotExe 'C:\Path\To\Godot_v4.7-stable_win64_console.exe'
```

Each timed run executes one million iterations. Both scripts run in batches of 250 iterations so the graph stays within its per-call execution limit. The harness warms up both scripts, checks the same expected result in every run, and reports the median and range across five trials. The result describes this CPU loop; other workloads may have a different ratio.

On November 14, 2026, we ran the saved graph headlessly in Godot 4.7 stable on a Ryzen 9 7900X. The prior release with compiled pin and variable slots took a median of **1,815.482 ms**. Compiling exact integer and boolean expressions reduced that to **155.796 ms**. Reusing execution edges reduced it further to **110.597 ms**, compared with **62.664 ms** for GDScript in that run. Each median is from five trials; every trial returned checksum `85457`.

In a follow-up measurement, the build with reused execution edges took **107.948 ms**. Looking up owner existence without fetching its C++ wrapper reduced that to **97.338 ms**. Writing the integer loop index directly to its existing slot reduced it to **76.525 ms**, compared with **62.428 ms** for GDScript. Each figure combines two process medians in a balanced run order, with five trials per process and the same verified checksum. Graphs still stop when their script is detached during a native callback.

For a separate comparison, the equivalent graph in [Orchestrator 2.5 stable](https://github.com/CraterCrash/godot-orchestrator/releases/tag/v2.5.stable) took **212.129 ms**; [2.5.1 RC2](https://github.com/CraterCrash/godot-orchestrator/releases/tag/v2.5.1.rc2) took **211.363 ms**. Both used official Windows release DLLs, the same Godot executable, loop bounds and batches. Both reported all verified results before a DLL unload crash on this machine; a repeat that stopped the isolated process after collecting results gave similar timings. These results apply to this arithmetic and branching graph.

The optimized evaluator covers exact integer and boolean expressions feeding **Branch** and **Set Variable**. At a live breakpoint, the debugger still evaluates and records each authored pin. Integer division truncates toward zero and remainder keeps the dividend's sign. Dividing the minimum integer by `-1` wraps to the minimum integer and gives remainder `0`; a zero divisor produces an error.

### Benchmark a health pickup

Open [gameplay_pickup.crimsonscript](../project/tests/visual_scripting/gameplay_pickup.crimsonscript). The **Event Graph** handles `Area3D.body_entered` and calls **Apply Pickup**. That function reads the body's health through `Object.get_meta`, adds 25 up to a limit of 100, and writes it with `Object.set_meta`. [The GDScript implementation](../project/tests/visual_scripting/gameplay_pickup.gd) does the same work.

```powershell
.\framework\project\tests\visual_scripting\Test-GameplayGraphPerformance.ps1 -GodotExe 'C:\Path\To\Godot_v4.7-stable_win64_console.exe'
```

The launcher runs headless with the release DLL. It sends the native signal once to the pickup for each of 8,192 prebuilt bodies. Each trial checks the final health of all bodies, including cases where the heal reaches the cap. Body setup, state reset, validation, and physics simulation are outside the timer. It reports the median of five trials and measures emission with no subscriber separately.

For an external run, pass `-OrchestratorAddon` with the extracted `addons/orchestrator` folder and `-OrchestratorGraph` with the matching pickup `.torch` graph. The latter must use `_on_body_entered(Node3D)` as the signal handler and implement the same heal. The fixture connects GDScript and Orchestrator through Godot's scene signal connection API; Crimson subscribes through its **On Body Entered** graph node. The launcher stops the isolated Orchestrator process after collecting results because the tested official DLLs crashed on normal unload on this machine.

On Godot 4.7 stable with a Ryzen 9 7900X, compiling signal and function argument transfer reduced Crimson's pickup time from **33.683 ms to 14.515 ms**. GDScript paired with the new build took **3.444 ms**. Orchestrator 2.5 stable took **8.854 ms**, and Orchestrator 2.5.1 RC2 took **8.862 ms**. Each figure is the midpoint of two process medians, with five trials per process. Every test checked each body's health and returned checksum `673792`. A separate physics smoke test confirmed that moving a real body into the area triggers the heal.

The same release builds also ran the million-iteration arithmetic fixture. Crimson measured **76.111 ms before** and **72.616 ms after** across two processes per build. Those timings measure a different workload from the pickup handler.

### Benchmark 24 FPS gameplay handlers

The [scenario manifest](../project/tests/visual_scripting/fps_benchmark/scenarios.json) defines the initial and expected state for combat, pickups, movement, and objectives. Each handler receives a Godot `Area3D.body_entered` signal, calls an authored function, and uses native Godot methods to change state. The launcher generates equivalent GDScript and Crimson files. Timed trials emit the same signal and verify every actor's metadata, group membership, position, and visibility afterward.

```powershell
$godot = 'C:\Path\To\Godot_v4.7-stable_win64_console.exe'
$suite = '.\framework\project\tests\visual_scripting'
& "$suite\Test-FpsGraphPerformance.ps1" -GodotExe $godot
```

To compare with the [official Orchestrator 2.5 stable release](https://github.com/CraterCrash/godot-orchestrator/releases/tag/v2.5.stable), extract its Windows addon and generate the equivalent `.torch` files from the same manifest. The converter uses the vendored Godot 4.7 API description to check native member signatures.

```powershell
$scenarios = "$suite\fps_benchmark"
$graphs = Join-Path $env:TEMP 'crimson-fps-orchestrator-graphs'
python "$scenarios\generate_orchestrator_graphs.py" "$scenarios\scenarios.json" `
    '.\framework\godot-cpp\gdextension\extension_api-4-7.json' $graphs
& "$suite\Test-FpsGraphPerformance.ps1" -GodotExe $godot `
    -OrchestratorAddon 'C:\Path\To\addons\orchestrator' -OrchestratorGraphs $graphs
```

The default run uses 2,048 events and five timed trials per scenario, plus warmup. It records raw samples and the fixture and binary hashes in `results.json`. Use `Compare-FpsGraphPerformance.ps1 -Crimson <results.json> -Orchestrator <results.json>` to join comparable runs into Markdown and CSV. The signal-only control is reported separately. The benchmark keeps scene creation, physics, networking, and rendering outside the timed region.

On the Windows x64 test machine with Godot 4.7 and a Ryzen 9 7900X, Crimson took **52.1% less time** than the starting build and **25.9% less time** than Orchestrator 2.5 stable by the geometric mean of the 24 scenario ratios. Crimson led both builds in all 24 cases. It took **1.77×** as long as the paired GDScript runs. Each backend ran twice, with five timed trials per process and full actor-state checks.
