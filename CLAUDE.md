# HotBiteEngine — agent notes

C++20 / DirectX 11 game engine (ECS architecture). Tools (SceneEditor, MaterialDesigner,
UiDesigner) live under `Tools/`, the engine under `Engine/Engine/`, the VS solution in
`Solution/HotBiteEngine.sln`.

## Building

```
Solution\build.bat [Debug|Release] [x64]          # whole solution
```

To build a single project (much faster), target it directly. The x64 configurations
use PlatformToolset v145, which only the VS 18 install provides — VS2022's MSBuild
fails with MSB8020, so use that one (check which edition is installed; this machine
has Community, older notes said Insiders):

```powershell
& 'C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe' `
    Solution\HotBiteEngine.sln /m /t:SceneEditor /p:Configuration=Debug /p:Platform=x64
```

Outputs land in `Solution/x64/<Config>/`. Engine.lib must exist (build the Engine
project first if not).

**Nothing in the solution declares a dependency on Engine, so `/m` will happily link a
tool against a stale `Engine.lib`.** After editing an engine *header* that is enough to
produce a class-layout mismatch between the tool's objects and the library's — which
shows up as a crash somewhere unrelated to what you changed (a resource `Init` failing
inside `LoadRTResources`, say, because the members it reads are at the wrong offsets),
not as a link error. Whenever an engine header changes, build in two steps and let the
first finish:

```powershell
& $msbuild Solution\HotBiteEngine.sln /m /t:Engine /p:Configuration=Release /p:Platform=x64
& $msbuild Solution\HotBiteEngine.sln /m /t:DemoGame`;SceneEditor /p:Configuration=Release /p:Platform=x64
```

The same race makes `MaterialDesigner`/`HotBiteTool` fail with `LNK1104: HotBiteTool.lib`
on a cold whole-solution build; re-running the build clears it.

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

**Directional shadows are cascaded** (`Components/Lights.h`, fitted in
`DirectionalLightSystem::Update`). A light's `cascades` (1..`MAX_SHADOW_CASCADES`, 4)
slices live in one `Core::DepthTexture2DArray`, not N separate maps: the pixel shader
picks its cascade with a *texture coordinate*, costing no extra shader register — the
ray tracers already sit at the 128-register `cs_5_0` limit — and the existing shadow
geometry shader (`ShadowMapCubeGS.hlsl`, shared with the point light's six cube faces)
fills every slice in one pass via `SV_RenderTargetArrayIndex`. Per-cascade matrices are
uploaded at a *fixed* `MAX_SHADOW_CASCADES` stride, so cascade c of light i is always
`[i * MAX_SHADOW_CASCADES + c]`; `DirLight::cascade_count` is what tells a shader how
many of a light's entries mean anything, and it is 0 until the first fit — an unwritten
slice has a zero matrix, which projects every point to the middle of an empty map.

**The cascades carry only what moves.** `Base::is_static` splits the casters in two and
that flag has no other meaning in the engine: a static caster is drawn *only* into the
light's single static map, a dynamic one *only* into the cascades. So the per-frame
shadow pass costs what the moving objects cost, and marking scenery static is the way
to make a heavy level cheap (the demo level marks its terrain, liquids and fixed props,
34 entities - everything whose rigid body is `STATIC`). The static map is one plain
`DepthTexture2D` fitted to the **widest** cascade, so it covers everything the cascade
set does; the price is density, since a static caster shades at that outermost cascade's
texels per unit even up close.

That map is re-rendered on `STATIC_SHADOW_REFRESH_PERIOD`, when the set of static
casters changes, *and* when `DirectionalLight::StaticShadowStale()` says the camera has
carried the widest cascade off the footprint it was rendered under. Without that last
one, walking far enough makes every piece of scenery stop casting until the period next
comes round - many seconds of a scene whose shadows are all missing.

Two things about the fit. It bounds each frustum slice with a **sphere**, not the tight
box of its eight corners: a tight box is ~30% smaller but its size and orientation
change as the camera turns, so every shadow edge crawls; a sphere is rotation-invariant,
and its centre is then snapped to whole shadow texels (in a *world*-anchored light
space, or the grid would move with the light's eye and defeat the snapping). The sphere
is the exact minimal one — the centre solves to `(n+f)(1+k²)/2` along the view axis,
clamped to the far plane — so nothing is given away beyond sphere-vs-box. And the fit
follows camera **rotation** as well as position, since the slice being bounded sits in
front of the camera; watching position alone (what the pre-cascade code did) leaves the
map behind when you turn on the spot.

Sizing changed with this: the old single map was `8 * screen width` texels covering
`width/50` world units, so coverage depended on monitor width and a 1080p screen asked
for a 15360-square map. Coverage is now fitted to the view, so resolution is a plain
authored number — `resolution` (which for directional lights has always *multiplied*,
unlike point lights) times 2048.

The debug views are **in the engine, not the editor**: `DIR_LIGHT_FLAG_DEBUG_CASCADES`
recolours a pixel by the cascade that shaded it, `DIR_LIGHT_FLAG_DEBUG_STATIC` by
whether the static map reaches it and whether a static caster occludes it
(`ApplyDirShadowDebug`). They have to be per pixel — which cascade a pixel falls in is
only known where the shadow lookup happens, and the volumes are always wrapped around
the viewer, so a wireframe overlay only ever shows the inside of a box you are standing
in. Both add a small floor on top of the shaded result so the colour stays legible in
shadow, which is where you most want to read it, and only one may be on at a time (they
recolour the same term). `Tools/SceneEditor/ShadowDebug.cpp` is only the switches
(`View/Shadow Cascades`, `View/Static Shadow Map`) and the legends, and its colours
**must** match `CASCADE_DEBUG_COLOR`/`STATIC_DEBUG_*` in
`PixelFunctions.hlsli`/`SimpleLight.hlsli`.

**Use `SampleCmpLevelZero` for shadow filtering, never `GatherCmp`.** Both compare
several depths in one instruction, which makes `GatherCmp` look like the right tool, but
it returns the four *raw* comparison results and deliberately bypasses the sampler's
filter. No amount of averaging them removes the stepping: the term can only change at
texel boundaries, and those are straight lines in shadow-map space — a staircase on
screen. `SampleCmpLevelZero` with the engine's `COMPARISON_MIN_MAG_MIP_LINEAR` sampler
blends the 2×2 comparison in hardware, so the term varies *within* a texel. The
directional kernel is a `DIR_PCF_RADIUS` square of those one texel apart
(`Defines.hlsli`); 5×5 of them look far smoother than the 121 `GatherCmp` calls they
replaced and cost roughly a fifth as much. For the same reason, never `round()` a
`SampleCmp` result — it throws away precisely the sub-texel blend being paid for.

**The depth pre-pass buffer is the frame's only depth buffer, and binding it is what
makes early-Z work.** `DrawDepth` fills two things: `RenderSystem::depth_view`, a plain
D32_FLOAT depth buffer, and `depth_map`, an R32_FLOAT *colour* target holding the world
distance to each surface — the thing water, lava, sky, particles, GI, the ray tracers and
autofocus sample as `depthTexture`. `DrawSky` and `DrawScene` then render *against*
`depth_view`, so the hardware rejects occluded fragments before the pixel shader runs.
The back buffer's depth buffer and any post-process pipeline's are never rendered into;
`RenderSystem` has no `depth_target` any more.

Three rules follow, and breaking any of them is subtle rather than loud:

- The state is `LESS_EQUAL` (`DXCore::depth_prepass`, plus `depth_prepass_read` for the
  sky, which must test without writing), never `LESS` — the main pass redraws the very
  surface the pre-pass recorded and would otherwise be rejected by its own value.
- The pre-pass rasterizes with `DXCore::depth_rasterizer`, which is `drawing_rasterizer`
  plus a small depth bias, and it is not optional. `DepthVS` reaches clip space through
  `world*view*projection`, while the main pass goes VS→HS→DS→GS with tessellation
  re-interpolating the position on the way, so the two agree only to the last few bits;
  without the bias that mismatch speckles every surface in the scene. Displacement
  (`MainRenderDS`) needs no slack of its own — it moves a front-facing surface *toward*
  the camera, which `LESS_EQUAL` already passes. The bias is in depth-format units, so
  the slack tracks the z-buffer's own precision instead of the flat 1.0 world unit the
  old in-shader test used (which was far too loose near the camera and far too tight
  far away).
- **Never add an occlusion test to a pixel shader.** Sampling `depthTexture` and
  `discard`ing is exactly what this replaced: the shader had to run in full to discover
  it was not needed, so it rejected no work at all. Reading `depthTexture` for what is
  *behind* a surface is still correct and still done — water and lava refraction,
  and `LavaPS`'s `dz_pcf`, which is not an occlusion test but the shoreline fade
  multiplied into the emitted colour at the end.

Depth writes stay **on** in `DrawScene` because the pre-pass deliberately skips
`ALPHA_ENABLED_FLAG | BLEND_ENABLED_FLAG` materials, anything with `draw_depth` false,
and the whole second-pass tree — all of which still have to occlude themselves.

**Buffer debugging lives in the texture mixer, not in a pass of its own.**
`TextureMixerCS` is where every contribution to the frame — scene colour, direct light,
bloom, emission, RT reflections/refractions, ReSTIR indirect, volumetric, dust, lens
flare, plus the depth/position/normal G-buffers — is still a separate texture, one
dispatch before it all becomes one image. So that is where `RenderSystem::eDebugBuffer`
switches the output to a single buffer instead of the mix: no extra dispatch, no extra
target, and what you see is exactly the bits the frame was about to be built from. The
selecting branch is uniform across the dispatch, so a debug frame does not pay to read
the buffers it is not showing.

Two things that path deliberately does:

- It **suppresses AA, motion blur, DOF and the lens effects** while a buffer view is up
  (`RenderSystem::IsDebugBufferActive`). A vignetted, depth-blurred normal buffer is not
  an inspection of anything. The stored settings are never written — all four are pushed
  to the GPU fresh every frame, which is the seam that makes this free — so they come
  back the moment the view goes to `off`.
- It applies a **gain** (`debug_gain`) to the colour buffers, because they are HDR:
  direct light saturates at 1x while indirect light sits far below it, and shown raw
  half the views read as black and look broken. `depth`/`position`/`normal` are *mapped*
  instead (exponential distance, a 10-unit repeating ramp, and the [-1,1] remap) and
  ignore the gain — a ramp has nothing to expose.

`RT_DEBUG_NO_GI_DENOISE` / `RT_DEBUG_NO_RT_DENOISE` are separate bits in the same value,
turning `GIAverageCS` and `DenoiserCS` into pass-throughs. They bypass the **temporal**
accumulation living in those same shaders as well as the spatial filter, on purpose:
with the history left on you are looking at an average of the noise rather than the
noise, and cannot tell which stage introduced what. Pair one with the matching buffer
view — `render debug_buffer indirect gi_denoise 0` is how you see ReSTIR's real sample
density.

**ReSTIR's ray pick must be jittered inside its stratum, and its phase must be
hashed.** `GIRayTraceCS` traces only 1–2 of a pixel's `ray_count` (16) cached
directions per frame, picking each by inverse-CDF from the pdf cache and weighting it
`W/(ray_count * n * pdf)` — which is only right if a ray is selected *with* probability
`pdf/W`. `GetRayIndex` therefore takes a random `jitter` in [0,1) saying where inside
the stratum to sample. Without it the target of stratum 0 is exactly 0 and the first
pdf entry is always positive (`RAY_W_BIAS`), so **stratum 0 returns ray 0 no matter how
small its probability**, then collects the `1/pdf` weight of a sample that was never
drawn that way — for a cold entry the `2/wis_size` clamp, ~16x the share one direction
out of 16 is worth. Which strata a pixel draws comes from `start`, so that one
over-bright pick lands wherever `start == 0` does: as `pixel.x + pixel.y + frame_count`
that was a bright diagonal one pixel wide repeating every 16 pixels and sweeping across
the scene each frame. Measured on the demo troll with `debug_buffer indirect
gi_denoise 0`, the profile of the raw GI against `(x+y) mod ray_count` peaked 5–8x
above every other phase; jittered, it drops into the measurement floor and the mean
radiance falls ~17% — the inflated energy was the artifact, so **a scene tuned against
the old GI will read slightly darker**. `start`'s pixel term is hashed for the second
half of the same reason: a linear ramp gives every pixel on a diagonal the same strata,
so any residual per-stratum variance is drawn as a line instead of as the noise the
kernel and denoiser exist to average away.

The packed value crosses into HLSL as a bare `uint` in three cbuffers and nothing
validates it, so `Shaders/Common/RenderDebug.hlsli` **must** stay in step with
`eDebugBuffer` and the `RT_DEBUG_*` constants in `RenderSystem.h`; the editor's label
list in `RenderSettings.cpp` is `static_assert`ed against the enum, and the automation
token list is generated from `RenderSystem::DebugBufferName` rather than duplicated.
`RenderSettings::ApplyHighDefaults` clears the whole thing on level load — a debug view
carried into a freshly opened level looks like the level rendering wrong.

**A game shader can mirror the engine's lighting cbuffer, and nothing checks it.**
`Tests/DemoGame/TerrainPS.hlsl` declares its own `externalData` block field for field
and then includes `Common/PixelFunctions.hlsli`, which indexes into it. An array sized
differently there — `DirPerspectiveMatrix[MAX_LIGHTS]` after cascades made it
`[DIR_SHADOW_MATRIX_COUNT]` — does not fail to compile: it shifts every field after it
and reads garbage matrices, which renders as a **solid black surface**. So any change
to that cbuffer means grepping outside `Engine/Engine/Core/Shaders` for the field names,
and the shader lives in the *game* project, so it only recompiles when that project
does. A black material whose shaders look right is this, not a material bug.

The two arrays are deliberately different lengths: the dynamic one is per light *per
cascade* (`DIR_SHADOW_MATRIX_COUNT`), the static one is per light (`MAX_LIGHTS`),
because static casters share one map.

A warning for any before/after measurement in this engine: it accumulates temporally
(GI/ReSTIR/autofocus), so a screenshot taken right after a state change is still
converging and differs from the settled frame by *far* more than whatever was changed.
Let it settle for several seconds and confirm two consecutive frames agree before
comparing anything.

### Making a screenshot A/B actually comparable

Settling is not enough, because the scene is *still* moving: `Sky::second_speed` advances
`second_of_day` every tick, which swings the sun's direction, its intensity and the sky
back colour, and the same `second_speed` scales the `time` uniform `DrawScene`/`DrawSky`
hand the shaders — which is what animates the clouds and the lava/water noise. Two shots
of one camera 40 s apart differ far more visibly than most changes being tested, and it
reads as a lighting regression that is not there.

**Stop the clock and clear the clouds first.** One automation command does both, and it
also pins the same sun for every run:

```powershell
editor-cli.ps1 -Dir $dir -Command `
    "set_component Sky Sky ""{'second_speed':0,'cloud_density':0,'second_of_day':43200}"""
```

`second_speed: 0` freezes the sun *and* zeroes the shader `time`, so clouds, lava and
water stop too; `cloud_density: 0` takes the cloud layer out of `SkyPS` entirely (it is
the noisiest thing in the frame); `second_of_day: 43200` is noon, and any fixed value
makes two runs start from the same lighting. Then place the camera, settle, and shoot.

What is left after that is the stochastic floor — GI/ReSTIR/denoiser samples that differ
run to run and never fully converge — so **the comparison is against that floor, not
against zero**, and the floor is big enough that ignoring it will make you "find"
regressions that do not exist. Measured on the demo scene, same binary, two launches,
three cameras: 0.4–3.0 % of pixels differ, mean |Δ| 0.005–0.051, max 15–25/255. The
lava camera's *same-build* pair was the single largest delta of any pair measured,
cross-build ones included.

So capture **two runs of each build** and compare the cross-build deltas against the
within-build ones, rather than reading one number. Mean |Δ| and % of pixels are the
robust statistics; max is one pixel out of 3.5 M and swings freely. A real difference
also *looks* different in a diff map — it lands on silhouettes, edges or whole surfaces,
where the floor is scattered structureless dither. `PIL` is available for diffing
(`ImageChops.difference`); `numpy` is not.

Menu items are registered in a `MenuCommand` registry (`SceneEditor.h`); new menu
entries added there are automatically clickable in the UI *and* scriptable via
`menu "<Menu>/<Item>"`, so keep using it instead of raw `ImGui::MenuItem` calls.

**Loading a level draws its own frames.** `SceneEditorApp::OpenLevel` passes
`World::Load` the progress callback the demo game uses and paints a progress overlay
(`RenderLoadingFrame`) from it — a bare Clear + one ImGui frame + Present, because the
load blocks the thread that would otherwise be rendering. Those frames open and close
an ImGui frame of their own, so `OpenLevel` must never be called from inside one:
anything drawing UI (the File menu, via `ProjectBrowser`) calls `RequestOpenLevel`,
which queues the path for the top of the next render tick. The automation `open_level`
command and the `--level` switch call `OpenLevel` directly — neither is inside a frame.
World::Load reports eight coarse phases, so the bar can sit still for a while inside a
long one (templates, typically); it covers 0–80% of the bar and the editor-side steps
after it (`World::Init`, editor data, post-process pipeline) fill the rest.

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

**A primitive collider is the entity's local box, in body space** (`Physics::AddCollider`).
Half extents *and centre* come from `Bounds::local_box` scaled by the Transform's scale;
the collider's own local rotation is the identity, because the rigid body already carries
the entity rotation and applying it twice tilts the shape against the box it was built
from. Nor may the extents *vector* be rotated — that is not how a box rotates, and it
mixes axes as soon as the rotation is not a multiple of 90°. Dropping the centre is the
other half of the same bug: the troll's mesh box sits 270 units (6.75 world) above its
feet, so a collider built from the extents alone spent half its height underground. The
mesh-collider branch has always worked this way (mesh-space triangles, identity local
transform), so the primitives now match it rather than having a convention of their own.
The fits: BOX is the box; SPHERE takes the largest half extent; CAPSULE runs along
whichever *local* axis stands most upright under the body's rotation
(`MostVerticalAxis` — never the longest extent, which for a rig is as often the arm span
as the height), with the radius the mean of the two cross half extents and the height
shortened by the caps so the shape ends where the box does. `Init`/`UpdateShape` take
the whole `box`, so every caller passes `local_box` — never `final_box`/`bounding_box`,
which the transform pass has already scaled.

**Editing a component's fields** goes through `ComponentOps::ApplyValue` /
`SetValue` / `RecordEdit` (`Tools/SceneEditor/ComponentOps.h`), never by poking the
struct alone: an edit that only touches the live component looks right until the
level is saved, at which point it is simply not in the file. Those helpers apply the
change through the component's own `FromJson`, write the serialized block into
`EditorState::component_deltas` (which is what `SceneSerializer` turns into the
entity's own `"components"` record — so the edit is per entity, not per template)
and push one history action. The Inspector's `SectionEdit` wraps the whole pattern
for widgets that edit in place: `Track` per widget, `Commit` at the end of the
section; use `Discrete` for a dropdown, whose value is picked in a popup and which
therefore never reports ImGui's activate/deactivate pair.

What is *not* editable per entity, and why: `Bounds` is a readout, because
`StaticMeshSystem::Update` re-measures a mesh entity's `local_box` from its mesh
every time the transform is dirty — a typed value would be overwritten within a
frame (a *template's* bounds are different: authored JSON the spawner clones, and
the Templates panel does override them). `Camera` and `Particles` are `Locked` in
the registry. `Material` has one editor of its own (see below).

Two engine-side consequences of that path being real: `Physics::FromJson` now
*rebuilds* a live rigid body when `type`/`shape` change (a setter alone would leave
the component describing a body that does not match), and `World::Init` re-seats a
body a component block already created rather than calling `Init` a second time —
which used to leave a ghost collider in the physics world for any entity whose
record carried a `Physics` block. `Mesh::StopAnimation` is the explicit "play
nothing", distinct from "never chose one" (which `SetData` leaves playing the first
animation); it serializes as `"animation": ""`, which is how an instance stands
still while its template animates.

**A placed instance's record is in spawn space, not world space.**
`World::SpawnInstance` composes the record with the template's base transform
(position added, rotation multiplied, scale multiplied), so anything storing a moved
instance back into `EditorState::placed_instances` must take that composition out
first — `Inspector::StoreInstanceTransform`, via
`World::GetTemplateBaseTransform`. Storing the live pose as-is makes every respawn
(a paste, the undo of a place, the next load) compose the base a second time: with
the troll template's 0.025 scale, a copy came out 40× too small.

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

`Core::MaterialProps` is uploaded raw — as the `material` cbuffer and as the
`objectMaterials[]` array the ray tracers index — so it is mirrored field for field by
`MaterialColor` in `Shaders/Common/PixelCommon.hlsli`, and adding or removing a
property means editing both plus the trailing padding that keeps `sizeof()` equal to
the row-padded size HLSL strides the array by. There is no ambient term and no alpha
colour key: both were fields that no loader ever wrote, so every material had them at
zero and the shaders read a constant (`ALPHA_ENABLED_FLAG` colour-keys against black,
which is what it always did). `Save` drops those retired keys from `source_json`
rather than round-tripping them forever — that erase list is where a removed property
goes so it stops appearing in `.mat` files.

Materials carry their own shader set (`MaterialShaderNames`), editable per stage in the
panel's Shaders section. Two constraints: the shader picker is a fixed list built from
the `*VS.cso`/`*PS.cso`/… naming convention, never free text, because
`ShaderFactory::GetShader` caches a shader under its name *before* checking it loaded as
the requested stage — one wrong entry poisons that name for the session; and changing
shaders in place must go through `World::SetMaterialShaders`, which calls
`RenderSystem::RefreshDrawable` on every user, because the draw trees are keyed by shader
tuple and `AddDrawable`'s own cleanup only evicts buckets whose *material* differs (so a
same-material key change would leave the entity drawing twice).

**Asset previews are their own forward pass, not the engine renderer**
(`Tools/SceneEditor/PreviewPass.h`). The material thumbnails (`MaterialPreview.h`) and
the Templates panel's model viewport (`ModelPreview.h`) both render offscreen through
it. Rendering a second *view* through `RenderSystem` was considered and rejected: it
owns ~40 backbuffer-sized targets (deferred light maps, GI, ReSTIR/RT, bloom, motion,
DOF, autofocus), a background ray-tracing thread and draw trees bound to one
coordinator, all sized and scheduled for exactly one view per frame — multi-view means
hoisting every one of those into a per-view context, i.e. a renderer rewrite, to get a
thumbnail. So a preview binds its own target, draws a few meshes and restores what was
bound. Both previews share `MaterialPreviewPS` (it is mesh-agnostic), so a material
looks the same in the swatch and on the model. They also share its three-light rig,
which is built *around the camera* (`PreviewPass::MakeLightRig`, directions passed in
as constants) rather than fixed in world space: the model viewport orbits, and a
world-fixed key left the model lit from behind over half the angles you can turn it
to.

Three things that path must respect. It runs *inside* the editor's ImGui frame, so
every binding it touches is saved and restored by `PreviewPass::ScopedState` — which
also unbinds the pixel-stage SRVs (material textures are bound as render outputs
elsewhere, and leaving them read-bound trips D3D's hazard detection) and switches off
HS/DS/GS. A `MeshData` owns no GPU buffers, only an offset pair into the world's single
`VertexBuffer` (`World::GetVertexBuffer()`), so drawing one means binding that and
using `indexOffset`/`vertexOffset` exactly as `RenderSystem` does. And template
transforms must be composed by hand: the templates coordinator runs no systems, so
`Transform::world_matrix` there is never computed and is still zeros.

**Framing a skinned mesh cannot use `MeshData::minDimensions/maxDimensions`** — those
measure the vertex positions as stored, which is the space *before* skinning. For a rig
whose bind pose sits away from where the animation puts it, that box is several times
the model's on-screen size; framing on it rendered the demo troll as a speck in an empty
viewport. `ModelPreview::MeasureSkinnedBounds` runs the same blend the vertex shader
does, once per animator rather than per frame (framing that tracked the pose would make
the model breathe in and out as it played). The preview also drives its *own*
`Components::Mesh` rather than the template entity's, because that component is what
`SpawnInstance` clones — advancing its clock would leave every instance spawned
afterwards starting mid-stride.

**Nor can a `Bounds`, for the same reason** — measure it through `Mesh::GetLocalBox`,
which is what `StaticMeshSystem::Update` and `Bounds::FromJson` call. For a skinned mesh
it returns the box of the *clip being played* (`MeshData::GetAnimationBox`), the union
over that clip's keyframes; only an unskinned mesh, or one with no animation, falls back
to the stored min/max. The bind pose is a T-pose for most rigs, so measuring it made the
demo troll's box 21.5 world units across for a model that is 9.7 — a readout nobody can
match to the model, late culling, and (since `Physics` sizes colliders from the local
box) a collider wrong in exactly the same way. Because the box changes with the clip and
not with the transform, `StaticMeshSystem::Update` re-runs when the measured box differs
from the one the Bounds holds, not only on `transform->dirty`.

The clip box is built once, when a skeleton is attached (`MeshData::BuildSkinnedBoxes`),
out of per-joint bind-space boxes transformed by each keyframe — never a vertex pass per
frame, and never a single pose (a box that tracked the pose would resize the collider and
pop in culling; one measured from a single frame would clip the model mid-stride). Which
weights count towards a joint's box is the one tuned number, `JOINT_BOX_WEIGHT_SHARE`;
its comment carries the measurements. `ModelPreview::MeasureSkinnedBounds` stays separate
and exact — framing one model offscreen can afford the vertex pass this cannot.

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

**Three asset layers, and they must not be mixed** — this is the spine of both the
level format and the editor's asset UI:

| layer | what it is | where it lives | placeable? |
| --- | --- | --- | --- |
| **model** | an imported `.fbx`: meshes, materials, collision shapes, animation clips | level's `"models"` array, `Assets/Objects/` | no |
| **template** | one concept ("troll"): a component block with values | level's `"templates"` array, `<assets>/Templates/*.tpl` | yes — the only one |
| **instance** | a template placed in this level | level's `"instances"` array | — |

An `.fbx` is a bag of assets, not an object: a character mesh comes out of one file and
its walk cycle out of another, so importing a file used to fill the Asset Browser with
"objects" called `troll_walk` and `space` that spawn nothing. `World::LoadModel` now
registers a file in its own registry (`model_entities` / `model_assets`, keyed by file
stem) and creates no template; `World::GetModelAssets` reports what it contributed,
which is what the Asset Browser's Models section lists and what "Create Template"
(`TemplateOps::CreateFromModel`) reads. Backwards compatibility is one line:
`World::GetTemplateEntities` falls back to the model registry, so a pre-split level
whose instances name an `.fbx` still loads, and saving migrates those entries into
`"models"`.

`World::CreateTemplate` (see the contract in `World.h`) keeps a template in two halves
on purpose: a template *entity* in the templates coordinator holding only what
`SpawnInstance` clones (Base/Transform/Bounds/Mesh/Material/Lighted), and the full
component JSON, which `SpawnInstance` applies to each spawned entity. Physics must stay
off the template entity — applying it there would create a rigid body for something that
is not in the scene. A template's mandatory components (`TemplateOps::IsMandatory`)
cannot be removed, since a template that cannot be spawned is not a template.

**The Templates panel authors templates and does not place them.** An edit there
changes the *definition* — what a "troll" is — while placing one changes the level,
and with both on one panel a click meant for the first routinely produced the second.
Putting an object in the scene is the Asset Browser's Place buttons (or the `place`
automation command), which is the one surface for it.

A template is stored either in its own `.tpl` under `<assets>/Templates/`
(referenced from the level's `templates` array) or inline in that array as
`{"name", "components"}`; `TemplateOps::SetStorage` moves it between the two and
`EditorState::inline_templates` records which is which. `.tpl` files save through
File/Save Templates, *but* File/Save Level flushes them first (unlike materials),
because the level names the files. `Tests/DemoGame/Templates/troll.tpl` is the
worked example: mesh, four named animations, material, scale and a dynamic collider.

Two traps in the same area. `World::Load` creates templates *after* the materials and
meshes sections, not in the templates phase — their blocks name materials and animation
clips that do not exist yet up there, and creating them early silently resolves every
material to the default white one. And template entities are registered under
`World::TEMPLATE_ENTITY_PREFIX`, because templates and models share one coordinator and
a template named after the object it represents ("troll") would otherwise overwrite the
FBX node of that name.

**A template owns its animations.** `Components::Mesh::clips` is the object's animation
library — logical name → imported clip, `{"idle": "troll_idle", "walk": "troll_walk"}` —
and `Mesh::SetAnimation` resolves through it before falling back to a raw clip name, so
game code plays a *role* (`SetAnimation("walk")`) and re-exporting the clip changes one
library entry instead of every caller. What the mesh reports afterwards is the name it
was asked for, so a template's vocabulary round-trips through save/load.

The animation *set* holding a clip (`World::GetSkeletons()`, one per FBX that carried
skeletons) is still what makes a clip playable, but nothing above the engine has to know
that: `Mesh::FromJson` looks the set up from the clip name (`World::FindAnimationSet`)
and attaches it. `"skeletons"` still works and is still the way to attach a set whose
clips are only chosen at runtime. The attachment is to the *shared* `MeshData`, so it is
visible to every entity using that mesh — same as it always was.

The editor surface is the Templates panel's **Animations** section
(`TemplatePanel::DrawAnimationsSection`, ops in `TemplateOps::AddClip`/`RemoveClip`/
`RenameClip`/`SetDefaultClip`): a table of name / clip / source model / default, an Add
picker grouped by model, and a Play button per row that auditions the clip through
`ModelPreview::View::clip_override` — a viewer-side override, never an edit. An instance
then picks its own out of the library from the Components panel (or
`set_component <entity> Mesh "{'animation':'walk'}"`), which is a per-entity override
and does not touch the template.

Entity rename and copy/cut/paste live in `Tools/SceneEditor/EntityOps.h` (read its
header comment before touching them). Two things there are easy to break: entity
*names* are the editor's stable key, so anything keyed by name in `EditorState` must
also be updated in `RenameEverywhere`; and a cut entity is *parked* (hidden, inert,
renamed with a `__cut_` prefix) rather than destroyed, so paste and undo keep working
— parked entities must stay filtered out of any new UI listing or save path.

Give screenshots a couple of frames after a state-changing command if the change
must be visible in the render (the channel already executes commands pre-frame and
captures post-frame, so single-batch `command + screenshot` is consistent).

That "couple of frames" is not optional for anything that changes the *ImGui layout* —
opening a panel, View/Reset Layout, or the first frame after an `imgui.ini` is restored.
ImGui needs a frame to settle a window into its dock node, and a batch of
`menu "View/Templates"` + `screenshot` captures the settling frame, in which the panels
render as nothing at all. A blank editor in a screenshot is that, not a crash: send the
screenshot as its own batch afterwards.
