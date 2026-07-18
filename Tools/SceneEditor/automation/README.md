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
| `state` | one-line JSON dump: project, level, selection, status message, entity/template counts |
| `open_project <dir>` | same as choosing a project root |
| `open_level <path>` | same as File/Open Level..., minus the file dialog (one level per session) |
| `menus` | lists registered menu commands and whether they are enabled |
| `menu <Menu/Item>` | executes a menu item, e.g. `menu "File/Save Level"` |
| `list_entities` | Entities panel contents with ids, positions and group membership |
| `select <name>` | selects an entity (same bookkeeping as clicking it in the Entities panel) |
| `focus` | frames the selected entity with the camera, same code path as double-clicking it in the Entities panel |
| `create_group <name>` | creates an (empty) entity group in the Entities panel tree |
| `set_group <entity> <group\|none>` | moves an entity into a group (`none` ungroups); an unknown group is created implicitly. Groups persist in the level JSON on save |
| `list_groups` | lists all groups and their member entities |
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
| `render <key> <value>` | changes one render setting, e.g. `render aa 0`, `render rt_quality high`; `render dof_autofocus 0\|1` toggles Marbles-style autofocus — on camera movement, focus is re-set to the scene depth at the view center and amplitude to Marbles' distance-based aperture (macro blur up close, everything sharp beyond ~20 units). On by default; setting `dof_focus`/`dof_amplitude` switches it off |
| `screenshot <png path>` | saves the backbuffer (scene + ImGui UI) as PNG at the end of the frame |
| `quit` | closes the editor |
| `debug_crash` | deliberate null write to exercise the crash pipeline; never responds (the process dies), so expect the driver to time out |

Screenshots are captured after the UI is rendered into the backbuffer, so what the
PNG shows is exactly what a user would see that frame.

Physics simulation is paused while editing (dynamic bodies hold the pose they were
authored/edited at, so transform edits and saves are exact); `menu "Edit/Simulate
Physics"` toggles it for previewing how objects settle.

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
