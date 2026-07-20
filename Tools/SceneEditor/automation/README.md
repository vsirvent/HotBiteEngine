# SceneEditor automation channel

A file-based remote control for driving the Scene Editor without mouse/keyboard,
built for scripted testing (including agent-driven sessions). It executes the same
code paths the UI does: menu commands go through the shared `MenuCommand` registry,
placement/import/transform commands reuse the Asset Browser and Inspector logic.

## Starting the editor

```
SceneEditor.exe --automation <dir> [--level <level.json>] [--project <project root>]
```

- `--automation <dir>`: enables the channel; `<dir>` is created if missing.
- `--level`: opens a level at startup (project root is derived from the level path
  by walking up to the folder containing `config.json`, unless `--project` is given).

The Debug x64 binary lands in `Solution/x64/Debug/SceneEditor.exe`; run it from that
directory so relative asset paths in level files resolve the same way the other tools do.

## Protocol

1. Driver writes `<dir>/command.txt` (write to a temp name, then rename) with one
   command per line. Quote arguments containing spaces with double quotes.
2. The editor consumes the file (deletes it), executes every line on its main
   thread before rendering the next frame, and atomically writes `<dir>/response.txt`
   at the end of that frame.
3. Responses echo each command as `# <command>` followed by `OK ...` or `ERR <reason>`
   plus any payload lines.

`editor-cli.ps1` wraps this: send commands, wait for and print the response.

```powershell
.\editor-cli.ps1 -Dir C:\tmp\ed -Command 'state'
.\editor-cli.ps1 -Dir C:\tmp\ed -Command 'select box1', 'set_position 0 2 0', 'screenshot C:\tmp\after.png'
```

## Commands

| Command | Effect |
|---|---|
| `ping` | liveness check, answers `OK pong` |
| `state` | one-line JSON dump: project, level, selection, gizmo mode, status message, entity/template counts |
| `open_project <dir>` | same as choosing a project root |
| `open_level <path>` | same as File/Open Level..., minus the file dialog (one level per session) |
| `menus` | lists registered menu commands and whether they are enabled |
| `menu <Menu/Item>` | executes a menu item, e.g. `menu "File/Save Level"` |
| `list_entities` | Entities panel contents with ids, positions and group membership (cut/parked entities are hidden) |
| `select [<name> ...]` | replaces the selection with the named entities (same bookkeeping as clicking, or Ctrl+clicking, them in the Entities panel). The last one becomes the primary — the entity the Components panel edits and the one `copy`/`cut`/`rename`/`focus` act on. No arguments clears the selection; an unknown name fails without changing anything |
| `add_select <name> ...` | adds the named entities to the current selection, the scripted Ctrl+click |
| `select_group <group> [add]` | selects every entity in a group, like clicking the group header; `add` extends the selection instead of replacing it |
| `list_selection` | lists the selected entity names, primary last |
| `delete` | deletes the selection — mesh entities are parked like a cut, placed instances despawn — as one undo step. The interactive Del key confirms first when several entities are selected; a scripted `delete` is already explicit and goes straight through |
| `rename <name> <new name>` | renames an entity (same as editing its name in the Components panel); rejects empty/duplicate/reserved names |
| `components <name>` | lists the components on an entity, in registry order; game components the editor cannot link are listed too (see below) |
| `add_component <name> <Component>` | adds a component with default values, like the Components panel's Add Component button. Fails for a component that is not registered, is already present, or cannot be constructed without picking an asset first (`Mesh`, `Material`, `Bounds`, `Lighted`) |
| `remove_component <name> <Component>` | removes a component, like the `x` on its Components panel section. Fails for `Base`/`Transform` (required) and `Camera`/`Particles` (engine-managed). Undoable, and restores the component's values, not defaults |
| `copy [<name>]` | copies the selection (or `<name>` if given) to the entity clipboard, like Ctrl+C |
| `cut [<name>]` | copies then removes the entity, like Ctrl+X (undoable; a cut source can still be pasted) |
| `paste` | creates a copy from the clipboard named `<original>_copy`, like Ctrl+V |
| `focus` | frames the selected entity with the camera, same code path as double-clicking it in the Entities panel |
| `create_group <name>` | creates an (empty) entity group in the Entities panel tree |
| `set_group <entity> <group\|none>` | moves an entity into a group (`none` ungroups); an unknown group is created implicitly. Groups persist in the level JSON on save |
| `list_groups` | lists all groups and their member entities |
| `materials` | lists every material: its `.mat` file (or `(none)` for an FBX-authored one), whether that file has unsaved edits, how many entities use it, and which one is selected |
| `select_material <name>` | selects a material and opens the Materials panel on it |
| `create_material <name> <mat file>` | creates a white material in one of the level's `.mat` files (use the `file=` value from `materials`) |
| `remove_material <name>` | retires a material; every entity using it is reassigned to the default white material. Undoable, users included |
| `set_material <entity> <material>` | repoints one entity at a different material, like the Components panel's Material combo |
| `shaders <material>` | prints the material's nine shader stages (`draw_vs` … `depth_ps`) |
| `set_shader <material> <slot> <file.cso>` | rebinds one stage. Slots: `draw_vs` `draw_hs` `draw_ds` `draw_gs` `draw_ps` `shadow_vs` `shadow_gs` `depth_vs` `depth_ps`. Fails without changing anything if the file will not load as that stage. Undoable |
| `save_materials` | writes every `.mat` file with unsaved edits. Materials are **not** saved by `save` - a `.mat` is a shared asset, not part of the level |
| `set_position x y z` | edits the selected entity's Transform like the Inspector fields |
| `set_scale x y z` | ditto |
| `set_rotation p y r` | Euler degrees, pitch/yaw/roll |
| `list_templates` | Asset Browser template list |
| `select_template <name>` | selects a template |
| `place <template>` | same as "Place at Origin" |
| `import <fbx path>` | same as File/Import Object..., minus the file dialog |
| `camera` | one-line JSON dump of the viewport camera: orbit `position`, rendered `world_position`, focus `target`, `rotation_deg` (pitch/yaw/roll), focus `distance` |
| `camera_pos x y z` | sets the camera's orbit position |
| `camera_target x y z` | sets the focus point the camera looks at / orbits around |
| `camera_rot p y r` | sets the orbit rotation, Euler degrees |
| `camera_orbit dx dy` | simulates a right-button drag of that many pixels (orbit) |
| `camera_pan dx dy` | simulates a middle-button drag (pan camera + focus point) |
| `camera_zoom steps` | simulates mouse-wheel steps (positive = toward the focus point, clamped before it) |
| `camera_fly fwd right up` | moves camera + focus point by camera-relative world units, like the WASD/QE fly keys |
| `render` | one-line JSON dump of the render settings (same keys as the Render menu) |
| `render <key> <value>` | changes one render setting, e.g. `render aa 0`, `render rt_quality high`; `render dof_autofocus 0\|1` toggles DOF autofocus, on by default. The focal distance is measured every frame on the GPU (`AutoFocusCS`) as the scene depth at the center of the view, smoothed over time; it never reaches the CPU, so the `dof_focus` key reports the *manual* distance, not the live one. Setting `dof_focus` switches autofocus off; `dof_amplitude` (the aperture) is independent of the mode |
| `render lens_aberration\|lens_grain\|lens_vignette <0..1>` | physical camera artifacts from the `LensEffect` post-process stage (chromatic aberration, film grain, vignette). 0 disables an effect, 1 is the strongest setting; out-of-range values are rejected. `render lens 0\|1` is the master switch, which zeroes all three without forgetting them |
| `colliders off\|selection\|all` | physics collider wireframe overlay (View/Colliders in the menu). Draws each collider exactly as reactphysics3d holds it — through the collider's local-to-body transform and the body's world transform — so a wireframe that does not wrap the mesh *is* a collider scale/offset bug. `all` is capped at 60000 segments and says so on screen when it truncates |
| `physics_info` | numeric counterpart of the overlay, for the selection: body type, active flag, collision shape, the entity's scale vs the scale baked into its collision mesh, and the collider's world AABB against the rendered mesh's — with an `ok`/`SUSPECT` verdict. The verdict only applies to mesh colliders; capsules/boxes/spheres approximate the mesh by design and report `n/a` |
| `screenshot <png path>` | saves the backbuffer (scene + ImGui UI) as PNG at the end of the frame |
| `rdoc_capture` | queues a RenderDoc capture of the frame being rendered; needs `--renderdoc` at launch. The `.rdc` is finalized after present, so poll `rdoc_last` for its path |
| `rdoc_last` | number of captures this session and the path of the newest one |
| `undo` / `redo` | steps the editor's undo history (same stack as Edit/Undo, Edit/Redo and Ctrl+Z / Ctrl+Y / Ctrl+Shift+Z); the `OK` line names the step applied, `ERR` when the stack is empty |
| `quit` | closes the editor |
| `debug_crash` | deliberate null write to exercise the crash pipeline; never responds (the process dies), so expect the driver to time out |

Screenshots are captured after the UI is rendered into the backbuffer, so what the
PNG shows is exactly what a user would see that frame.

The viewport gizmo has translate/rotate/scale modes, switched via the Edit menu
(`menu "Edit/Gizmo: Rotate"` etc., current mode in `state` as `gizmo_mode`) or the
1/2/3 keys in the UI. Interactive drags edit the same Transform channels as
`set_position`/`set_rotation`/`set_scale`.

Physics simulation is paused while editing (dynamic bodies hold the pose they were
authored/edited at, so transform edits and saves are exact); `menu "Edit/Simulate
Physics"` toggles it for previewing how objects settle.

Copy/cut/paste work on both editor-placed template instances (paste spawns a fresh
instance of the same template) and mesh entities including FBX-authored ones (paste
clones them via `World::CloneEntity`). Lights, cameras and the sky can't be copied.
Renames, copies (`clones`) and deletions (`removed_entities`) persist on Save Level
and are reconstructed on the next load. Cut does not destroy an authored entity
immediately - it is *parked* (hidden, inert, filtered out of `list_entities`) so
paste and undo keep working; it is genuinely gone after a save + reload.

Scene mutations (transform edits, `place`, group commands, rename/copy/cut/paste,
`delete` - from any surface: UI, menu, or automation) record into a single undo
history; `undo`/`redo` walk it. Operations on a multi-entity selection - a gizmo
drag, a group move, a delete - record *one* action covering all of them, so they
undo in a single step rather than one per entity.
Every *new* command that mutates the scene must record itself there too - the
contract and the how-to live in `Tools/SceneEditor/EditorHistory.h`. Interactive
drags (gizmo, Inspector fields) coalesce into one action per drag; selection,
camera, render settings and `import` deliberately don't record (see the header).

The `camera_*` commands drive the same `EditorCamera` code paths as the interactive
viewport controls (right-drag orbit, middle-drag pan, wheel dolly, WASD/arrows +
Q/E fly with Shift = fast / Ctrl = slow — see EditorCamera.h). Note the rendered
`world_position` reported by `camera` lags one frame behind a movement issued in
the same batch: the CameraSystem consumes the change on the next world tick, so
query it in a follow-up batch.

## Crash diagnosis and debugging

Two independent layers:

**In-app crash reporter (always on).** `CrashHandler` (installed in `wWinMain`)
catches any unhandled SEH exception and writes into the automation dir (or next to
the exe when `--automation` isn't used):

- `crash.txt` — exception code/address and a symbolized stack of the crashing
  thread with function names and `file:line` (from the PDB next to the exe), also
  echoed to stderr.
- `crash.dmp` — a minidump; open it in Visual Studio or WinDbg for full state.

So even a plain run never crashes silently: read `crash.txt` and you have the
faulting line.

**Live debugging under cdb (WinDbg package).** Needs `winget install
Microsoft.WinDbg` once; the scripts locate `cdb.exe` inside the package via
`Get-AppxPackage`.

- `debug-run.ps1 -AutomationDir <dir> [-Level <level.json>]` — runs the editor
  under cdb. The editor behaves normally (automation channel included). On a crash
  the debugger breaks first and `debug-run.log` in the automation dir ends with
  `!analyze -v` (faulting source line, exception details) plus every thread's
  stack; on a clean exit it just records the exit. Start it in the background and
  drive the editor as usual.
- `stacks.ps1 [-ProcessId <pid>]` — non-invasively attaches to a *running*
  SceneEditor, prints all thread stacks with source lines, and detaches without
  disturbing it. Use when the editor hangs or stops answering the channel.
