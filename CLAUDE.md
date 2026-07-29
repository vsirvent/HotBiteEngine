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

**Debug x64 does not link in this tree**, and not for any reason in the code: every tool
wants `reactphysics3d.lib`, which is a CMake build living in `React3d/out/build/` and only
ever built for **x64-Release**. No project in the solution builds it, so a Debug link ends
at `LNK1104: reactphysics3d.lib`. Build and test against Release; a Debug *Engine* build
(and with it the Debug `.cso` set) does succeed, which is worth doing after a shader change
so the Debug shaders are not left half-old.

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

Do not trust *any* measurement taken from a partially rebuilt tree after an engine header
changed. The failure is not a crash you can attribute: a mismatched `RenderSystem.h` shifts
every member after the one that moved, so render targets come out bound to each other's
memory and buffers read as plausible-but-wrong - a motion buffer that is uniformly zero, a
camera matrix that appears frozen while the image plainly moves. It also corrupts the heap,
which surfaces as an access violation in an unrelated system several frames later
(`PrepareLights`, `AudioSystem`, `RtlFreeHeap`) with a different stack each run. If results
stop making sense, rebuild both steps clean before debugging anything else.

## The regression suites — run them, and add to them

**Any change to the engine or the Scene Editor MUST pass the automation tests
before it is considered done. No exceptions.** Run the relevant suite *before*
the change (to have a baseline — some failures predate you) and *after*, and
**every new feature adds its own tests**. Both suites need the Release build
(`-Config Debug` does not link here — see above), and both use exit code =
number of failures, so either one gates a commit on its own.

There are two, and which ones apply depends on what was touched:

| changed | run |
| --- | --- |
| Scene Editor only | the editor suite |
| a game (Marbles, DemoGame) only | that game's suite |
| **the engine** (`Engine/`) | **both** — an engine change reaches every consumer, and the two suites cover different parts of it (the editor exercises authoring and serialization, the game exercises gameplay, physics and level loading) |

```powershell
# Engine + Scene Editor: ~240 tests over 18 files, ~3 minutes
Tools\SceneEditor\automation\tests\Run-Tests.ps1                    # everything
Tools\SceneEditor\automation\tests\Run-Tests.ps1 -Suite '10-parts*' # one file
Tools\SceneEditor\automation\tests\Run-Tests.ps1 -Test '*undo*'     # one test

# Marbles (separate repo, ../Marbles): boot, menus, the four levels, player, render
..\Marbles\Marbles\automation\tests\Run-Tests.ps1
..\Marbles\Marbles\automation\tests\Run-Tests.ps1 -Suite '03-levels*'
```

Running the game suite after an engine change is not belt-and-braces: the engine
bugs found this way — a `float4` typedef whose `alignas` silently did nothing
(faulting the hand-written `_mm_load_ps` in `Defines.h`), and `Scheduler::Update`
invoking a timer that a callback earlier in the same iteration had removed and
whose owner it had then deleted — were both invisible to the editor suite and
both crashed the game outright.

There is no in-process harness because nothing in the editor runs without a D3D
device and a loaded `World`. The automation channel below *is* the seam, so a test
written against it drives the same code paths the UI does. Each suite gets a
generated throwaway project (`New-TestProject.ps1`) and its own editor process —
one level per session, and a suite that saves or deletes must not reach the next.
How to write one, what is in scope, and the traps (screenshot comparisons are
against a stochastic floor, motion vectors need a run of frames rather than a
capture) are in `Tools/SceneEditor/automation/tests/README.md`.

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

### Logging

`Engine/Core/Log.h` is the engine's logger: `LOG_TRACE/DEBUG/INFO/WARN/ERROR/FATAL`,
each capturing `__FILE__`/`__LINE__` automatically (never pass them by hand). A host
calls `Log::Init("<file>.txt", level)` — the Scene Editor and Marbles both do, first
thing in their constructors, at `Debug`. Every line is flushed immediately, because the
usual reason to reach for this is a crash a moment later and a buffered line a crash
never flushes is worse than no line at all.

Lines go to the file *and* to a bounded in-memory ring the Scene Editor's **View/Log**
panel renders (docked full-width at the bottom, with a level combo and Clear). `Init`
truncates: one run, one log.

**Reach for this before the debugger.** The two engine bugs behind the Marbles crashes
were both found by adding a few `LOG_DEBUG`s and reading the file — one printed
`alignof(MaterialData)` from both sides of the `Engine.lib` boundary and settled an
alignment question a disassembly had only made ambiguous. `Log::Enabled()` short-circuits
before the varargs, so a `LOG_TRACE` left in a hot loop costs one comparison until
someone opts into that level.

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

**"Previous frame" belongs to the renderer, and to nothing else.**
`RenderSystem::LatchPreviousFrame`, called at the very end of `Draw()`, is the one place
all three of the things a motion vector is measured against are stored: the camera's
`prev_view_projection`, every entity's `Transform::prev_world_matrix`, and every skinned
mesh's `Mesh::prev_joint_gpu_data`. Each of them used to be latched by the system that
produces it - `CameraSystem::Update`, `StaticMeshSystem`/`PhysicsSystem::Update`,
`Mesh::Update` - and every one of those runs on the **background thread**, on a timer
unrelated to the render tick. Two ways that goes wrong, and both were live:

- Latched *next to* the value it is supposed to lag behind, the pair is identical. That
  is what `StaticMeshSystem::Update` did (`prev_world_matrix = world_matrix` right after
  computing `world_matrix`), so **every entity reported zero motion** - a moving or
  animating object had no motion vector, no motion blur, and the object half of every
  temporal reprojection was dead. The camera had the same bug in a subtler form: its
  ticks where nothing moved copied the current matrix over the previous one, so unless
  the tick that moved the camera was the last one before a `Draw`, the motion was gone.
- Latched correctly but on the wrong clock, it measures one of *that system's* ticks
  rather than one frame - the magnitude is then whatever the two rates happen to be.

`Camera::prev_view_projection` is gone from the component so it cannot be reintroduced
there; the other two are only written by the latch. `drawables` (a flat
`EntityVector<DrawableEntity>`) exists for this: the render trees are keyed by shader and
material, and the latch has to touch each entity exactly once.

**A skinned mesh animating in place moves, and the pose is the only record of it.**
`MainRenderVS` skins each vertex twice - once with `joints`, once with `prev_joints` - and
carries the second result down the chain as `prevPos` (object space) to
`GSOutput::prevObjectPos`, which `MainRenderPS` multiplies by `prevWorld` to get
`pos0_map`. Both halves are needed and neither substitutes for the other: `prevWorld`
alone cannot see an animation (a rig walking on the spot has one world matrix all frame),
and the previous *pose* alone cannot see the object move. This is why `GSOutput` carries
`prevObjectPos` and not the current `objectPos` it used to - a geometry shader that
generates its own vertices (`TerrainGS`'s grass) has no previous pose and writes its
current position there, and an unskinned `VertexOutput::prevPos` is just `position`.

Adding a field to `VertexOutput`/`HullOutput`/`DomainOutput` reaches the *game's* shaders
too (`TerrainVS` feeds `MainRenderHS`), and fxc only warns (X3578) about the one that
forgets to write it. The cost of the second joint array is 16 KB more in the main render
VS cbuffer, uploaded per draw whether the mesh is skinned or not; the alternative -
splitting the joints into their own cbuffer so static geometry stops paying for the first
one either - means every VS that declares `joints` and a `CopyAllBufferData` that no
longer copies everything.

**Verifying anything about motion vectors needs the thing to be moving on consecutive
frames.** One `camera_orbit`, or one `set_position`, moves it for a single frame, and
`CameraSystem`/`StaticMeshSystem` only recompute their matrices on the next background
tick, so a `camera_orbit` + `screenshot` batch (or a `set_position` + `screenshot` one)
captures a frame with no motion in it nearly every time - the object reads as static and
it looks like the bug is back. Drive the channel at frame rate instead - write
`command.txt`, wait for the editor to delete it, write the next - and screenshot every
frame of the run. `render debug_buffer motion` shows the result: **grey (143,143,143)** is
not moving, red/green deflect with +x/+y, and it is the one mapped view that honours
`debug_gain`. It is worth counting the pixels that differ from that grey rather than
eyeballing it - a static object is the same colour as the background, so "I cannot see
the troll" means "the troll is not moving", not "the troll is not drawn". A moving object
lands around 10% of the viewport, camera motion at 100%.

The three cases are separate code paths and a check of one says nothing about the others:
an entity moved by hand or by physics (world matrix), a rig animating in place (previous
pose), and the camera (`prev_view_proj`).

Writing `command.txt` from PowerShell 5.1, use `[System.IO.File]::WriteAllText` with
`ASCIIEncoding` - `Set-Content -Encoding utf8` prepends a **BOM**, which makes the first
command of every batch fail to parse while the rest run. A frame-rate driver built that
way silently never moves anything and produces a screen full of "not moving".

**Temporal reprojection needs the *previous* view-projection, and one pass had to be
told.** `DenoiserCS` (reflections/refractions) and `GIAverageCS` (ReSTIR indirect) both
find last frame's history by taking `prev_position_map` — the surface's previous pose
through its previous world matrix — and projecting it to a screen position. That
projection must use `prev_view_proj`. `DenoiserCS` used `mul(view, projection)`, i.e. this
frame's camera, which reprojects object motion but drops camera motion entirely: for
static geometry `prevWorld == world`, so the previous world position projects straight
back onto the same pixel and the history is fetched from where the surface is *now*.

It survived because the blend weight `saturate(0.7f - motion * 50.0f)` discards history
outright above ~0.014 NDC of motion, about 10 pixels a frame — fast movement threw the bad
fetch away, and below that the wrong pixel is close enough that it read as slight softening
during slow pans rather than as ghosting. `prev_view_proj` had simply never been plumbed
into that pass's cbuffer; `GIAverageCS` has always had it. If you add another temporally
accumulating pass, that constant is the thing to check first.

**A pixel shader that builds its own `RenderTargetRT` must fill every field of it.**
`TerrainPS`'s *grass* branch (`!any(input.tangent)`) constructs its own output rather than
delegating to `MainRenderPS`, and left `pos0_map`/`pos1_map` unassigned. That does not skip
the export — the ROP writes whatever was in the register — so `MotionCS` read uninitialized
memory as the grass's world position now and last frame. Grass does not move, so both are
`input.worldPos`, the same convention `WaterPS` and `LavaPS` use. FXC only warns (X3578)
about this, so it builds clean either way.

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

**Normal smoothing is a `Mesh` component flag, and it acts on the shared mesh asset.**
The importer gives every polygon *corner* its own frame, so a control point used by
several faces is cloned and each face keeps its own normal — that is flat shading.
Smoothing sums the frames of all the clones of one control point and hands the sum back
to each. `MeshData` keeps both halves (`flat_frames` and `smooth_groups`, 40 bytes a
vertex) so the choice is re-derivable instead of baked, which is the only reason it can
be a flag at all: models load long before any component block is read, so a value that
had to reach `FBXLoader` could never come from a level.

The scope is the *asset*, exactly like `Mesh`'s `skeletons` key and exactly like the
`.NoSmooth` node-name suffix it replaces — every entity drawing that mesh changes, and
two entities asking for different values is last-writer-wins. The suffix is still read
at import as the default, because it is the only way the existing `.fbx` files say it;
a level with no `"smooth"` key keeps whatever the import decided.

Three things there that are not guessable:

- **`Mesh::ToJson` writes `smooth` unconditionally**, never "only when it differs from
  the import default". Every `FromJson` reads a missing key as *leave alone*, and undo
  works by replaying an earlier `ToJson` (`ComponentOps::RecordEdit`) — so a key omitted
  because it matched the default cannot be restored. The symptom is an undo that reports
  success and changes nothing. The same trap waits for any new component field.
- **Propagating skin weights to the cloned vertices is not part of the smoothing** even
  though it used to live inside it. Only the control point carries the weights the
  importer read out of the cluster; its clones are made before those exist. Gated on
  `smooth`, a flat-shaded *skinned* mesh comes out with every clone unskinned — latent
  until this made flat shading something you can ask for.
- The world vertex buffer is `IMMUTABLE` and holds every mesh, so `MeshData::SetSmooth`
  only rewrites the CPU copy and marks it dirty (`World::SetMeshSmooth`);
  `World::FlushMeshBuffers`, called once per frame from the editor tick between frames,
  does the one `Unprepare`/`Prepare` rebuild. Sum, never average, when fusing — the
  shaders normalize, so averaging would only change every lit pixel of every existing
  scene for nothing.

**A level-of-detail chain is a list of alternate meshes on the mesh asset, and only
three numbers ever change.** `Core::MeshData::lods` is finest first with `lods[0]`
being the mesh itself, so selection code has no special case for full detail; the
alternates are authored meshes (`World::SetMeshLods`, the Mesh component's `"lods"`
key), never anything the engine generates. `RenderSystem::SelectLods` then writes
`Components::Mesh::index_count`/`index_offset`/`vertex_offset` — the only things the
five `DrawIndexed` sites read — and leaves `Mesh::data` pointing at the full mesh, so
the `Bounds`, the collider sized from them, the BVH the ray tracers walk and the
skeleton being animated are all still LOD0 and none of them wobble with the camera.
Scope is the *asset*, exactly like `smooth` and `skeletons`; the one per-entity part
is `Mesh::lod_enabled`, which pins the object the camera is always on.

Four things there that are not guessable:

- **It runs once per frame, over `drawables`, before anything draws** — next to
  `CheckSceneVisibility` at the top of `Draw`, for the same reason `LatchPreviousFrame`
  is over that flat list: the trees hold an entity once per shader tuple and it has one
  geometry per frame. Before, because the depth pre-pass records the surface the main
  pass is tested against — a switch between those two passes is not a pop, it is a hole.
- **`LOD_AUTO` compares a *derived* ratio against screen coverage.** `ratio` is
  `lod->vertexCount / lods[0].vertexCount`, so the reduction is measured from the
  geometry supplied rather than typed next to it, and a level is eligible once its ratio
  is at least the fraction of the viewport the model covers (times `lod_bias`, which
  scales the detail *demanded*: 2 asks for twice the vertices and so switches at half the
  coverage). Coverage is the bounding *sphere* through `Camera::GetFrustumParams` —
  rotation-invariant, so a model turning on the spot keeps one coverage — measured
  against view *depth*, not distance, since the screen is a fixed angle wide.
- **The selection is hysteretic** (`LOD_HYSTERESIS`, 10%): the metric has to pass a
  boundary by a margin before the answer follows it, in whichever direction it is
  moving. Without it a model parked near a threshold re-crosses it on the noise of the
  bounds fit and swaps silhouette every frame, which is far more visible than either
  level being wrong.
- **A skinned mesh only takes a stand-in rigged to the same skeleton**
  (`CompatibleSkinning`): the vertices carry joint *indices* into whatever skeleton
  LOD0 is animating, since the level never reaches `Mesh::data`. Same skeleton object
  (levels exported as sibling nodes of one `.fbx`) passes immediately; across files the
  joints must agree in count and order. An unskinned stand-in on a skinned mesh is
  refused outright — it would draw in its bind pose while the model animates.

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

**A multi-material is a stack of material layers, and it belongs to a *material*, never
to an entity** (`Core::MultiMaterialData`, authored in the Materials panel's
Multi-Materials tab — `Tools/SceneEditor/MultiMaterialPanel.h`). It is a named asset like
a material: stored in a `.mat` file's `multi_materials` array, saved by File/Save
Materials, and worn by a material through `MaterialData::multi_material_name`. Non-null
`MaterialData::multi_material` is what makes `RenderSystem` take the multi-texture path,
where the layers *replace* that material's own diffuse/normal/spec/ao/height maps (the
rest of it — emission, opacity, flags, shaders — still applies).

Per entity was the obvious alternative and is not expressible: the draw trees are keyed
by material (`RenderTree` is `ShaderKey -> MaterialData* -> …`), so one bucket is drawn
with a single set of layer constants no matter how many entities it holds. The old code
kept the stack on the `Material` *component* and bound whichever component the bucket
happened to hold first, which was only ever right because nothing wrote it.

Each layer names a source material for its maps plus the rules that decide *where* it
lands: a mask image with a **channel** (four layers off one RGBA splat map, which is what
makes a single big mask over a terrain practical), optional inversion and noise, and two
range rules — **slope** on `dot(world normal, up)` (1 flat, 0 vertical, -1 overhang: the
snow-on-flat-ground knob) and **height** on world Y. They all multiply, so a layer lands
only where every rule it declares agrees. Four things there are not guessable:

- The range fade sits **outside** the range, not straddling its edge. `smoothstep(min -
  fade, min + fade, x)` puts the boundary itself at the fade's midpoint — weight 0.5 —
  so a caller who sets `slope_max: 1.0` meaning "include flat ground" gets flat ground at
  *half* strength. `multiRangeMask` fades over `[min-fade, min]` and `[max, max+fade]`
  instead, and `[min, max]` is fully included.
- A layer that supplies no normal map must fall back to `{0.5, 0.5, 1.0}`, not to zero
  (`MultiTextureNotEnabledDefault`). The value is decoded `*2-1` as a tangent-space
  direction, so zero decodes to `(-1,-1,-1)`: a normal facing away from every light. A
  perfectly good blend rendered **pitch black** because of this, and it looks like the
  stack is not binding rather than like a normal-map bug.
- `getValues` needs the normal **in world space**. `MainRenderDS` has it in object space
  at the call site (it transforms it a few lines later), so passing it as-is tilts every
  slope rule with the model's own rotation.
- `Rebuild()` derives the `TEXT_DIFF`/`TEXT_NORM`/… bits from which maps the layer's
  source material actually has, so it must run *after* the materials have loaded their
  textures — which is why `World::Init` calls `ResolveMultiMaterials()` after the
  `m.Init()` loop and not with it.

The layer `op` crosses into HLSL as a bare `uint` and nothing validates it, so the
`TEXT_*` constants in `Core/Material.h` and the `MULTITEXT_*` ones in
`Shaders/Common/MultiTexture.hlsli` **must** stay in step. The two `float4` arrays
carrying the slope/height rules are declared in every shader that includes
`MultiTexture.hlsli` (nine of them, `Tests/DemoGame/TerrainPS.hlsl` included — an include
cannot declare the constants it reads), which is the same trap the lighting cbuffer has.

**Mask painting is a tool, and deliberately outside the undo history**
(`Tools/SceneEditor/MaskPaint.h`). A session edits one layer's mask channel; the canvas is
bound as that layer's mask through `MultiMaterialData::SetLiveMask` so strokes show up
without a round trip through disk, and only Commit writes a file. A stroke is a pixel edit
to an image asset — undoing it would mean restoring file bytes, the same reason
File/Import Object is out of scope — so Cancel is the escape hatch instead; *which file a
layer points at* is authoring data and is undoable.

Two hazards that both produced real crashes: `Commit` must clear the live mask **before**
rebuilding and before releasing the session texture, or `Rebuild` re-adopts the very SRV
about to be destroyed and the next frame binds freed memory; and every write to those
arrays needs `RenderSystem::mutex`, because the editor's render tick is not the thread
commands run on. Neither shows up on a single paint/commit cycle — it took four in a row.

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
| **template** | one concept ("troll"): a component block with values, optionally carrying other templates as `parts` | level's `"templates"` array, `<assets>/Templates/*.tpl` | yes — the only one |
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

**A template may also be composed: it carries other templates as `parts`.** That is the
troll with its sword, the house made of six pieces. A part is a *reference*
(`{"name", "template", "attach", "bone", "position"/"rotation"/"scale", "components"}`
in the `.tpl` beside `components`), resolved at spawn — so one sword template can be a
part of any number of composed templates, editing it reaches all of them, declaration
order in a level never matters, and a composed template can itself be a part
(`World::CanComposeTemplate` refuses the cycle where it is authored, `MAX_COMPOSED_DEPTH`
catches the merely absurd).

Offsets are in the **root entity's own frame**: its rotation turns them, its position
carries them, its scale does not (a part is a whole object with a scale of its own). An
attached part and a detached one at the same offset therefore land in the same place, and
flipping `attach` never moves anything.

Three rules that are not guessable:

- **An attached part carries no rigid body.** Bone-attached, its pose changes every frame,
  so a collider is stale the moment it is made; DYNAMIC/KINEMATIC, `PhysicsSystem::Update`
  writes body poses straight into the Transform and silently undoes the attachment. Both
  are stripped at spawn through `ApplyComponents`' own `"remove"`, which also stops
  `World::Init` handing one back. A STATIC body survives and is seated at the composed
  world pose — `World::ComposedWorldPose`, which `Init` uses for *every* entity, because an
  attached part's Transform is an offset and not a place in the world. A composed object
  that moves carries its collision on the root.
- **The spawner owns the naming**, `<instance>__<part>` recursively, and
  `World::InstanceEntityNames` is the one place it lives — the editor's
  `AssetBrowser::InstancePartNames` calls it rather than reproducing the rule, which is
  what keeps removal, undo and the reload bookkeeping from drifting.
- **`Base::parent_bone` is the only parenting path that reads the parent's whole world
  matrix** (`local * joint * parentWorld`, so the parent's scale reaches the child).
  The plain path composes the parent's position and rotation and *deliberately not* its
  scale, because `FBXLoader` gives every imported child node a global transform *and* a
  parent, so the two are already double-counted; changing that would move existing
  content. Scaling a composed root instead rescales its attached parts in the editor
  (`Inspector::PropagateScaleToAttachedParts`), matching what `SpawnInstance` does with
  the instance scale on reload.

A bone socket reads `Components::Mesh::joint_pose_data`: the joint's *model-space* matrix,
which is the keyframe blend before `model_to_bindpose` is folded in. It is filled only
while `TrackJoints()` is on (the first socket to resolve turns it on), and it must not be
confused with the skinning matrix — `Core/Particles.h` wants that one, in the separate
`joint_cpu_data` that `Components::Mesh` never fills (a latent bug in the particle path,
untouched here).

`World::CreateTemplate` (see the contract in `World.h`) keeps a template in two halves
on purpose: a template *entity* in the templates coordinator holding only what
`SpawnInstance` clones (Base/Transform/Bounds/Mesh/Material/Lighted), and the full
component JSON, which `SpawnInstance` applies to each spawned entity. Physics must stay
off the template entity — applying it there would create a rigid body for something that
is not in the scene. A template's mandatory components (`TemplateOps::IsMandatory`)
cannot be removed, since a template that cannot be spawned is not a template.

The editor surface for parts is the Templates panel's **Parts** section
(`TemplatePanel::DrawPartsSection`, ops in `TemplateOps::AddPart`/`RemovePart`/`SetPart`),
plus `TemplateOps::CreateFromSelection` — Edit/Create Template from Selection with more
than one entity selected, which is the "arrange it in the scene, then keep the
arrangement" path. It references the template each selected object was placed from and
creates one (as its own undo step) for anything that was not.

**Authoring a composed template runs in both directions**, and the scene one is what
gets used: place the object, drag its parts in the viewport, then
`TemplateOps::ApplyInstanceToTemplate` (Edit/Apply Instance to Template,
`apply_to_template`) writes them back. It is the exact inverse of `SpawnInstance`'s
composition and has to undo all three steps in the order they were applied —
`MeasureSpawnedPart`: the root's pose for a detached part, the instance's scale (which a
bone-riding part never carried, the parent's world matrix having scaled it), and the part
template's own base transform. It applies the parts and their component deltas and
deliberately not the instance's placement or the root's scale/rotation, which compose
into every instance and would be folded in twice. The per-instance overrides it consumes
are dropped in the same undo step, or that instance would stay pinned while every other
one followed later edits.

**The root of a composed selection is the entity picked *first*** (`Selection::Root`,
`selected_entities.front()`), not the primary — the primary is the most recent pick,
which is what anchors a shift-range and what the Components panel edits, so it changes
under you as you gather a selection. The Entities panel draws the root amber with a `*`
prefix (ASCII: the default ImGui font has no bullet glyph and renders one as `?`), and
`list_selection` marks it `[root]`.

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
