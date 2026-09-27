# hotbite-editor MCP server

Gives an agent (Claude Code, or later the editor's own agent panel) the Scene Editor's
view of a level: entities, templates, models, materials, textures and the rendered
viewport. It is a thin layer over the editor's file-based automation channel
(`../automation/README.md`), so every answer is what the editor's own panels show.

`editor_command` is the full automation channel, the same one the regression suites
drive, so an agent can do anything a test can. `--read-only` leaves it out. The
other tools only look; the one thing they move is the viewport camera (`screenshot`
with a pose), which is editor view state rather than level data.

The editor's own Claude panel (View/Claude, `ClaudeAgent.h`) starts this server with
`--embedded`, which drops `editor_launch`. That agent runs inside the editor it would
otherwise launch a second copy of.

## Running it

No dependencies and no `npm install`. The server speaks MCP's stdio transport
(newline-delimited JSON-RPC) directly, because `@modelcontextprotocol/sdk` needs
Node 18 and this machine has Node 14.

```
node Tools/SceneEditor/mcp/server.js [--dir <automation dir>] [--read-only] [--embedded]
```

The automation dir is `--dir`, else `HOTBITE_AUTOMATION_DIR`, else
`%TEMP%\hotbite-editor`. The server talks to whatever editor is running on that dir.
`editor_launch` starts `Solution/x64/Release/SceneEditor.exe` on it (override the
path with `HOTBITE_EDITOR_EXE`), from the output folder so relative asset paths
resolve. An editor you started yourself works as well, if it was given the same
`--automation <dir>`.

The repository's `.mcp.json` registers it for Claude Code sessions opened at the
repository root.

## Tools

| tool | what it returns |
| --- | --- |
| `editor_launch` | starts the editor (optionally with a `level`) and waits for it to answer; does nothing if one already answers |
| `editor_status` | the `state` JSON |
| `scene_summary` | state, camera, selection, entities, groups, templates, models, materials, in one batch (one frame) |
| `list_assets` | one kind: templates, models, materials, multi_materials, textures, texture_folders, meshes, animations, splat_clouds, groups |
| `entity_details` | every component of an entity as the JSON the Components panel edits |
| `template_details` | a template's component blocks, parts and animation library |
| `model_details` | what an imported `.fbx` contributed |
| `material_details` | texture maps, shader stages, tessellation/displacement |
| `texture_users` | which materials and layers use a texture |
| `screenshot` | the editor window as a JPEG (downscaled to 1568 px by default); with `position`/`target` it moves the camera first, waits for the pose to apply and lets the frame settle |
| `editor_query` | raw read-only automation commands, for everything else |
| `editor_command` | any automation command, in one batch (one frame). It refuses `quit`, `debug_crash` and menu items ending in `...`, which open a modal file dialog on the editor's main thread and stall the channel until a person answers. `timeout_s` goes up to 600 s for big imports |

`editor_query` takes an allow-list of commands (`READ_COMMANDS` in `src/tools.js`),
each with a maximum argument count. The count is part of the rule, not a usage check:
`render`, `material_surface` and `shader_sources` read with no extra arguments and
*write* with them. An argument may not contain a double quote or a line break (the
editor's tokenizer has no escape, and a line break would start a second command).

## Things that are not obvious

- **One batch at a time.** The channel is a single `command.txt`, so every call goes
  through one queue. Concurrent MCP calls are safe, and they run one after another.
- **A batch that goes unanswered is taken back.** On timeout the server deletes its
  own `command.txt`, so a late editor cannot run it later in the middle of
  another call. If a `crash.txt` newer than the call exists, it is quoted in the error.
- **A camera move does not apply on the next frame** (see CLAUDE.md). `screenshot`
  polls `camera` until the pose matches, then waits `settle_frames` (default 20),
  because GI and denoising accumulate over frames.
- **Screenshots include the editor UI.** It is the backbuffer, the same as the
  automation `screenshot` command. The Entities list and the Components panel in the
  picture are context the agent can use.

## Tests

```
node Tools/SceneEditor/mcp/test/run.js      # exit code = failures
```

This runs the real `server.js` over stdio against a fake editor that answers the
channel from canned responses. No editor or GPU is needed, and it takes a few seconds.
It covers the protocol handshake, batching, quoting and injection refusal,
the write refusal in `editor_query`, the screenshot path, concurrent calls, and the
unresponsive-editor path.
