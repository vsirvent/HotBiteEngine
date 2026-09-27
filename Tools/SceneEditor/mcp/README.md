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
| `meshy_status` | whether a Meshy API key is configured (never the key itself), the account's credit balance, any configured spending caps, and recent ledger entries |
| `meshy_ledger` | the local record of every Meshy generation actually run and charged |
| `meshy_text_to_3d` | generates a new 3D model from a text prompt via Meshy AI and imports it as a template. Spends credits - see below |
| `meshy_image_to_3d` | generates a new 3D model from a reference image via Meshy AI and imports it as a template. Spends credits |
| `meshy_retexture` | generates new PBR texture maps for a project model via Meshy AI and applies them to an existing material. Spends credits |

## Meshy generation: setup and the approval contract

`meshy_text_to_3d`/`meshy_image_to_3d`/`meshy_retexture` (`src/meshy-client.js`,
`src/meshy-credits.js`, `src/meshy-tools.js`) call the real Meshy API against the
user's own account and spend real credits, so two things are deliberately not the
same as every other tool here:

- **The API key is never typed into the agent's chat.** Anything in that
  conversation becomes part of a transcript, which is exactly where a secret must
  not go. Run `Tools/SceneEditor/mcp/setup-meshy-key.ps1` once, directly in
  PowerShell - it writes only `%TEMP%\HotBiteMeshy\config.json`
  (`HOTBITE_MESHY_DIR` overrides the folder), never a file under the project or
  this repo. `meshy_status` is how the agent confirms a key is present afterward,
  without ever seeing it.
- **Every credit-consuming call is two calls.** The first, with no
  `confirm_token`, only looks up Meshy's own published price for the exact
  parameters given (deterministic - not a guess, and there is no free "quote"
  endpoint to ask instead) and returns a token; nothing is sent to Meshy yet. The
  tool's own description tells the agent to state that cost and stop, and only
  call again - with identical arguments plus `confirm_token` - after the user has
  explicitly approved in a later message. The token is single-use, expires after
  15 minutes, and is rejected if any other argument changed since it was issued.
  This is a chat-level convention (the tool has no way to know whether a human
  really looked at the estimate), not a hard technical gate - an optional
  `max_credits_per_task`/`max_credits_per_day` in `config.json` is the hard
  backstop that holds regardless.
- **The ledger is the ground truth.** Whatever the estimate quoted, the reply and
  `meshy_ledger` always report the real `consumed_credits` Meshy's task response
  carries. A mismatch (Meshy changed a price, or the local table drifted) is
  called out in the text rather than silently trusted.

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

It also runs the Meshy tools against a fake HTTP server standing in for
`api.meshy.ai` (`HOTBITE_MESHY_API_BASE`) and a throwaway `HOTBITE_MESHY_DIR`, so no
real network call and no real credit is ever touched by the suite: the
estimate/confirm token round trip, an unknown/expired/argument-mismatched token
being refused, a configured spending cap being enforced, and `meshy_status` never
echoing the key.
