# SceneEditor / engine regression tests

A test suite for the editor's automation channel and, through it, for the engine
behaviour that channel can observe. Every new feature should arrive with tests
here, and every change should leave the existing ones passing.

```powershell
Tools\SceneEditor\automation\tests\Run-Tests.ps1                     # everything
Tools\SceneEditor\automation\tests\Run-Tests.ps1 -Suite '1?-parts*'  # one file
Tools\SceneEditor\automation\tests\Run-Tests.ps1 -Test '*undo*'      # one test, across files
Tools\SceneEditor\automation\tests\Run-Tests.ps1 -ListSuites
Tools\SceneEditor\automation\tests\Run-Tests.ps1 -Keep               # keep projects/screenshots
```

The exit code is the number of failed tests. A failing run keeps its artifacts -
generated project, level file, screenshots, `crash.txt`/`crash.dmp` - under
`%TEMP%\HotBiteEditorTests\<timestamp>\<suite>\`, and prints the path.

Needs a built `Solution\x64\Release\SceneEditor.exe` (`-Config Debug` or `-Exe`
to point elsewhere). Release, because **Debug x64 does not link in this tree** -
see the note in `CLAUDE.md`.

## How it works

There is no in-process unit harness because nothing in the editor runs without a
D3D device and a loaded `World`. The automation channel *is* the seam: it drives
the same code paths the UI does (menu commands through the `MenuCommand` registry,
placement/import/transform through the Asset Browser and Inspector), so a test
written against it exercises the real thing rather than a mock.

- `Run-Tests.ps1` - discovers `suites\*.tests.ps1`, gives each one a fresh
  generated project and its own editor process, runs it, reports, cleans up.
- `TestFramework.psm1` - session management, response parsing, assertions,
  screenshot statistics.
- `New-TestProject.ps1` - generates the fixture project. Read this first when a
  test asserts "3 boxes": that file is what makes it three.

**One editor process per suite**, because `SceneEditorApp::OpenLevel` refuses a
second level in a session - and because a suite that saves, imports or deletes
must not reach the next one. That costs a few seconds of level load per suite,
which is why suites are grouped by subject rather than split finely.

## Writing a test

A suite is a `.ps1` file under `suites\` whose first lines declare its fixture:

```powershell
# fixture: empty
# description: What this file is about.

Test 'set_position edits the selected entity' {
    SendOk 'select box_a' | Out-Null
    SendOk 'set_position 1 2 3' | Out-Null
    Assert-Vector3Near -Expected @{ x = 1.0; y = 2.0; z = 3.0 } `
        -Actual (Get-Position -Session $Session -Entity 'box_a')
}
```

In scope inside a suite:

| | |
|---|---|
| `Send 'a', 'b'` | one batch, returns a result per command (`.Status`, `.Text`, `.Payload`) |
| `SendOk 'a', 'b'` | the same, asserting every command answered `OK` |
| `Shot 'name'` | screenshot into the suite's folder, returns the path |
| `$Session` `$Project` `$Assets` `$LevelPath` `$ShotDir` | the live editor and its project |
| `Get-State` `Get-Render` `Get-Camera` `Get-Component` `Get-Position` `Get-TemplateInfo` `Get-EntityNames` `Get-PlacedInstance` | typed readers |
| `Assert-True/False/Equal/NotEqual/Near/Vector3Near/Match/NotMatch/Contains/NotContains/Ok/Err/FileExists/FileNotExists` | assertions; failure = a thrown message naming expected vs actual |
| `Step-EditorFrames` | render N frames without changing anything |
| `Get-ImageStats` `Get-ImageDifference` | screenshot statistics |
| `Skip-Test -Reason` | end the test as not-applicable rather than as a failure |

Two syntax rules, both learned the hard way:

- **Commands go in one array argument**: `Send 'a', 'b'`, never `Send 'a' 'b'`.
  PowerShell 5.1 stringifies an array bound through `ValueFromRemainingArguments`
  and would glue the batch into one unparseable line.
- **JSON arguments are single-quoted**: `set_component box_a Base "{'pass':2}"`.
  The channel's tokenizer strips double quotes, so a double-quoted object never
  arrives intact - this is the same rule the channel README states.

## Fixtures

Generated fresh per suite by `New-TestProject.ps1`, never the demo level: that
one takes tens of seconds to load, its contents drift, and half these suites
write to the project.

**`empty`** - no FBX at all. `World::CreateTemplate` turns an absent Mesh into the
built-in cube, which is enough for entities, selection, transforms, components,
groups, clipboard, history, materials, templates, parts, camera, render settings
and persistence. Loads in ~2 s. Contains:

- lights `ambient` and `sun`
- `camera_rig` - a `Camera` component is what makes `EditorCamera` find a camera
  at all; without one every `camera_*` command answers "no camera"
- `box_a` (-3,0,0), `box_b` (3,0,0), `box_c` (0,0,3) - instances of `tf_box`
- templates `tf_camera`, `tf_box` (inline) and `tf_marker` (a `.tpl` file)
- materials `TestRed`, `TestBlue` in `Assets\materials\test.mat`

**`models`** - the same plus a copy of the demo troll (skinned mesh, `idle`/`walk`
clips, its own `.mat`), a `tf_troll` template scaled and turned upright like the
demo's, and a ground slab for shadows to land on. ~13 MB of copying and ~5 s of
FBX load, so only the suites that need a real mesh use it.

**`lods`** - `models` plus the two sky domes (`Space`, 2880 vertices, and `Sky`,
36), which is the least a level-of-detail chain can be tested with: two unskinned
meshes of the same kind, one an exact 1.25% of the other. Both are scaled down to
0.02 in the `tf_dome` template, because at their authored size a sky dome fills
the view from anywhere and every screen-area assertion would read a coverage of 1.
The troll comes along so "a skinned mesh will not take an unskinned stand-in" has
both halves.

## Suites

| file | fixture | covers |
|---|---|---|
| `01-channel` | empty | liveness, batching, tokenizer, `state`, the menu registry |
| `02-entities` | empty | listing, selection (primary vs root), focus, rename |
| `03-transform` | empty | transform edits, multi-selection semantics, spawn-space records |
| `04-components` | empty | add/remove/read/edit, per-entity deltas, Physics rebuild |
| `05-groups` | empty | groups, group selection, persistence |
| `06-clipboard` | empty | copy/cut/paste/delete and the parking that keeps a cut undoable |
| `07-history` | empty | LIFO, redo invalidation, what deliberately does not record |
| `08-materials` | empty | `.mat` assets, assignment, retirement, shader slots, saving |
| `09-templates` | empty | authoring, component blocks, `.tpl` vs inline, placement |
| `10-parts` | empty | composed templates, offsets, spawn naming, apply-back |
| `11-camera` | empty | pose, orbit/pan/dolly/fly, the one-frame render lag |
| `12-render` | empty | every render setting, debug buffers, denoiser bypasses |
| `13-physics` | empty | collider overlay, `physics_info`, primitive fits, simulate preview |
| `14-persistence` | empty | save + reload in a second editor process |
| `15-models` | models | the three asset layers, import, model → template |
| `16-animations` | models | the animation library, defaults, per-instance overrides |
| `17-skinning` | models | clip-measured bounds, colliders, bone sockets, smoothing |
| `18-engine-render` | models | motion vectors, G-buffers, shadow debug views |
| `19-multimaterials` | empty | multi-material layer stacks, orientation/altitude rules, mask painting, rendering |
| `20-lods` | lods | the LOD chain on the mesh asset, switching by screen area and by distance, per-entity opt-out |
| `21-shaders` | empty | shader hot reload: the source index, recompiling into the running editor, include-graph change detection, the watcher, and a broken shader changing nothing |
| `23-splatrender` | empty | the Gaussian splat pass: that a cloud reaches the frame, follows its transform, writes depth, and answers its per-entity knobs |
| `24-createentity` | empty | Add/Entity: an entity built from nothing - its mandatory components, undo, delete, and the `created_entities` record it saves and reloads through |

## Things that will bite you

**Screenshot comparisons are against a floor, not against zero.** The engine
accumulates temporally (GI/ReSTIR/autofocus), so two captures of the same settled
scene differ by a few percent of pixels. `Step-EditorFrames` before capturing, and
compare shares rather than exact images.

**Motion vectors need the max over a run of frames, not one capture.**
`StaticMeshSystem` and `Mesh::Update` recompute on the background tick, which does
not line up with the render tick, so a given frame of a move may have no motion in
it. `Measure-MotionOverFrames` drives one batch per frame with the capture *in the
same batch* and takes the largest share. A single capture is a coin flip.

**Camera motion reaches the motion buffer only intermittently** - roughly one frame
in twenty, and only when the move and the capture are in the same batch. Object
motion and animating-rig motion are reliable. That test therefore *reports*
(`[skip]` with the measurement) instead of asserting, so a run stays green and
readable; the assertion is written directly below the skip, ready to switch on
once the camera path is deterministic. The test's own comment has the evidence.

**A shader test edits a copy, never the tree.** `21-shaders` writes its shaders
into a scratch folder registered with `shader_sources add`, which wins the name
collision - so the engine's own `.hlsl` files are never touched and a failure
halfway through leaves the repo clean. That folder has to sit **outside
`$Project`**: the editor registers the open project's folder as a source folder of
its own (a game keeps its shaders there), so a scratch copy under the project is
found again by the project scan the moment the explicit entry is removed.

**A reload answers when it is queued, not when it is done.** `reload_shaders`
hands the work to a compile thread - fxc takes 36 s on `GIRayTraceCS` - so a test
polls `shader_reload_status` until it reads `idle` before asserting on anything
the render decides.

**A test that fails skips its own cleanup.** Suites that change global state
(render settings, debug buffers, the camera) set what they need at the *start* of
each test rather than restoring it at the end, so one failure does not cascade.

**Editor state is shared within a suite**, in file order: a test that renames or
deletes something affects the ones after it. Between suites nothing is shared.
