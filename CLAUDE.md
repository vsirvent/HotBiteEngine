# HotBiteEngine — agent notes

C++20 / DirectX 11 game engine (ECS architecture). Tools (SceneEditor, MaterialDesigner,
UiDesigner) live under `Tools/`, the engine under `Engine/Engine/`, the VS solution in
`Solution/HotBiteEngine.sln`.

## Building

```
Solution\build.bat [Debug|Release] [x64]          # whole solution
```

To build a single project (much faster), target it directly. The x64 configurations
use PlatformToolset v145, which only the VS 18 (Insiders) install provides — VS2022's
MSBuild fails with MSB8020, so use this one:

```powershell
& 'C:\Program Files\Microsoft Visual Studio\18\Insiders\MSBuild\Current\Bin\MSBuild.exe' `
    Solution\HotBiteEngine.sln /m /t:SceneEditor /p:Configuration=Debug /p:Platform=x64
```

Outputs land in `Solution/x64/<Config>/`. Engine.lib must exist (build the Engine
project first if not).

## Testing the Scene Editor yourself (no human needed)

The Scene Editor has a file-based automation channel so it can be launched, driven,
and visually verified without mouse input. Full command reference:
`Tools/SceneEditor/automation/README.md`.

Typical loop:

```powershell
# 1. launch (from the output dir so relative asset paths resolve)
Start-Process -WorkingDirectory Solution\x64\Debug Solution\x64\Debug\SceneEditor.exe `
    -ArgumentList '--automation', $dir, '--level', $levelJson

# 2. drive it and check responses
Tools\SceneEditor\automation\editor-cli.ps1 -Dir $dir -Command 'state'
Tools\SceneEditor\automation\editor-cli.ps1 -Dir $dir -Command 'select box1', 'set_position 0 2 0'

# 3. capture what's on screen and inspect the PNG with the Read tool
Tools\SceneEditor\automation\editor-cli.ps1 -Dir $dir -Command 'screenshot C:\...\shot.png'

# 4. shut down
Tools\SceneEditor\automation\editor-cli.ps1 -Dir $dir -Command 'quit'
```

### Crashes and hangs

- Any crash writes `crash.txt` (symbolized stack with file:line) and `crash.dmp`
  into the automation dir — check for them whenever the channel times out.
- To run under a debugger: `Tools\SceneEditor\automation\debug-run.ps1
  -AutomationDir $dir -Level $level` (background); on crash, `debug-run.log` in the
  automation dir ends with `!analyze -v` + all thread stacks. Uses cdb.exe from the
  WinDbg package (already installed; `winget install Microsoft.WinDbg` if missing).
- Editor hung? `Tools\SceneEditor\automation\stacks.ps1` dumps all thread stacks of
  the running process non-invasively.
- `debug_crash` automation command deliberately crashes the editor to validate this
  pipeline (never responds — the driver times out by design).

### GPU profiling

`Tools/SceneEditor/automation/profiling/` captures frames via RenderDoc and prints a
per-pass GPU timing table — `capture-frames.ps1` then `analyze-captures.ps1`, full docs
in that folder's README. Two things that invalidate results if ignored: profile the
**Release** build, and **close the editor before replaying** (a running editor competes
for the GPU and distorts passes unevenly enough to reorder the ranking). Passes are named
by matching shader bytecode hashes to the built `.cso` files, because the engine emits no
debug markers and every shader's entry point is `main` — so analyze against the same
configuration you captured.

Menu items are registered in a `MenuCommand` registry (`SceneEditor.h`); new menu
entries added there are automatically clickable in the UI *and* scriptable via
`menu "<Menu>/<Item>"`, so keep using it instead of raw `ImGui::MenuItem` calls.

**Undo/redo rule:** every editor command that mutates the scene or its editor
bookkeeping MUST push an `EditorHistory::Action` right after the mutation succeeds,
from whichever surface triggered it (panel widget, menu, automation). The full
contract — closure rules (capture entity *names*, not ids), the LIFO guarantee,
drag coalescing, and what is deliberately out of scope — is documented at the top
of `Tools/SceneEditor/EditorHistory.h`; follow the existing helpers
(`Inspector::ApplyTransform`, `Outliner::SetEntityGroup`,
`AssetBrowser::PlaceTemplate`, `EntityOps::RenameEntity`) as the pattern.

**Edit/Simulate Physics is a preview, not an edit** (`Tools/SceneEditor/PhysicsPreview.h`):
switching it on snapshots every non-static body's transform, switching it off rewinds
the scene to those snapshots, and a transform edited *while* it runs retargets its own
snapshot so the rewind lands on the edited pose. The consequence for any new code that
writes a `Transform`: do it under `Core::physics_mutex`, held from before the first
field is written until the commit is done (`Inspector::EditTransformLock`). While the
simulation is live the physics thread writes body poses into those same Transforms, and
an unlocked read-modify-write intermittently latches a mid-fall pose as the rewind
target — the failure is rare and looks like "disabling physics moved my object".

**Materials** are authored in the Materials panel (`Tools/SceneEditor/MaterialPanel.h`)
and live in `.mat` files, which are shared assets referenced by a level rather than
part of it — so they save through File/Save Materials (`save_materials`), *not*
File/Save Level, and `World` tracks which `.mat` each material came from. Two traps:
`MaterialData::Save` round-trips the keys `Load` ignores via `source_json`, so don't
rebuild the JSON from scratch; and `RemoveMaterial` retires a material instead of
erasing it, because `FlatMap` removal relocates another element and would dangle every
`Material::data` pointing at it. Material property editing has exactly one
implementation, `MaterialPanel::DrawMaterialProperties`, reused by the Components
panel — edit values through it or the change is neither undoable nor ever saved.

Materials carry their own shader set (`MaterialShaderNames`), editable per stage in the
panel's Shaders section. Two constraints: the shader picker is a fixed list built from
the `*VS.cso`/`*PS.cso`/… naming convention, never free text, because
`ShaderFactory::GetShader` caches a shader under its name *before* checking it loaded as
the requested stage — one wrong entry poisons that name for the session; and changing
shaders in place must go through `World::SetMaterialShaders`, which calls
`RenderSystem::RefreshDrawable` on every user, because the draw trees are keyed by shader
tuple and `AddDrawable`'s own cleanup only evicts buckets whose *material* differs (so a
same-material key change would leave the entity drawing twice).

**ImGui gotchas, both seen in `Inspector::DrawComponentSection`:**
- Don't put a `SmallButton` over a `CollapsingHeader` with `SameLine` — the header spans
  the full width and eats the click, so the button looks inert and the section just
  collapses. Use the header's own `p_visible` close button.
- A widget whose label equals its component's name collides with the section header:
  `PushID("Material")` + `CollapsingHeader("Material")` + `BeginCombo("Material")` all
  hash to one ID, the header owns it, and the combo draws and hovers but never opens.
  The body is therefore wrapped in its own `PushID("body")` scope — keep it that way, and
  suspect ID collisions whenever a control renders and highlights but won't activate.

When driving the UI with synthetic clicks, note the backbuffer/screenshot size is not
always the client size — scale screenshot coords by `client/backbuffer` before
`ClientToScreen`. And never tap ALT at a window that is already foreground to break the
foreground lock: it opens the system menu, whose modal loop inside `DefWindowProc` hangs
the editor's message pump (shows up as `uxtheme!OnDwpSysCommand` in `stacks.ps1`).

Entity rename and copy/cut/paste live in `Tools/SceneEditor/EntityOps.h` (read its
header comment before touching them). Two things there are easy to break: entity
*names* are the editor's stable key, so anything keyed by name in `EditorState` must
also be updated in `RenameEverywhere`; and a cut entity is *parked* (hidden, inert,
renamed with a `__cut_` prefix) rather than destroyed, so paste and undo keep working
— parked entities must stay filtered out of any new UI listing or save path.

Give screenshots a couple of frames after a state-changing command if the change
must be visible in the render (the channel already executes commands pre-frame and
captures post-frame, so single-batch `command + screenshot` is consistent).
