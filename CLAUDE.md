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
fails with MSB8020, so use that one. **Check which edition is actually installed**
rather than trusting the path below — this note has now been wrong in both
directions, so run it, do not read it:

```powershell
Get-ChildItem 'C:\Program Files\Microsoft Visual Studio\18' | Select-Object Name
```

As of 2026-09-02 that answers **Community** (`Run-Tests.ps1`'s own error message
assumes the same):

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

**A shader change can silently not be built, and the build still says it succeeded.**
Two separate traps, and together they cost three rounds of an A/B that all secretly
ran the same `.cso` and read as "the feature does nothing":

- **FxCompile does not track `.hlsli` includes here.** Edit a shared header, rebuild,
  and every `.cso` that includes it is stale. `AdditionalInputs` metadata on the
  FxCompile item does *not* fix it (tried). The `InvalidateShadersOnSharedHeaderChange`
  target at the bottom of `Engine.vcxproj` touches the dependent `.hlsl` files instead,
  because that is the input FxCompile does track — extend its list when a new shared
  header appears.
- **Never write a shader file with PowerShell's `Set-Content -Encoding utf8`.** It
  prepends a **BOM**, and fxc rejects a BOM in an `#include`d file with
  `error X3000: Illegal character in shader file`. The compile then fails while the
  stale `.cso` stays on disk and keeps being loaded, so the engine runs yesterday's
  shader. Use `[System.IO.File]::WriteAllText(path, text, [System.Text.UTF8Encoding]::new($false))`.
  This is the same BOM trap as `command.txt`, in a place where it fails far more quietly.

So: **after any shader edit, confirm the `.cso` timestamp actually moved**, and when a
measurement is on the line use `/t:Engine:Rebuild` rather than an incremental build.
A `.cso` older than its `.hlsl` is the tell.

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

Marbles is a project **in this solution**, so build it here (`/t:Marbles`) before
running its suite — `Solution\x64\Release\Marbles.exe` is what the runner drives, and
a stale one measures the engine it was linked against rather than the one you changed.
A note here once said that checkout had no `automation/` and no longer compiled; both
were false as of 2026-09-02 — it builds clean and the suite runs 36/36 in under two
minutes. Verify before believing either claim again.

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

**A Gaussian splat cloud is drawn by a compute pass, not by the draw trees**
(`RenderSystem::DrawSplats`, `Shaders/Splats/`). Five dispatches per visible cloud:

| pass | what it does |
| --- | --- |
| `SplatPreprocessCS` | projects every Gaussian; records the nearest one per 16x16 tile |
| `SplatBinCS` pass 0 | histogram of (tile, depth bucket) |
| `SplatScanCS` | scans each tile's buckets into offsets within that tile |
| `SplatBaseCS` | scans the per-tile totals into each tile's base in a shared pool |
| `SplatBinCS` pass 1 | scatters the entries into their slots |
| `SplatRasterCS` | one group per tile, over that tile's contiguous slice |

The rasterizer fills the *same* G-buffer `MainRenderPS` does — scene colour, light map,
`depth_map`, the `rt_ray_sources0/1` pair (world position and normal, with material
scalars packed into their `w`), and `position_map`/`prev_position_map` — so a cloud is lit
by the level's own lights and shadows (it stores albedo/normal/spec rather than baked
radiance), and it shows up in the ordinary frame and in every debug buffer, never in a
channel of its own. Only `bloom_map` is skipped, a splat having no emission.

Filling all of it is what makes a cloud exist to everything downstream: without the ray
sources it cannot appear in a reflection or be gathered from by ReSTIR, and without the
position pair `MotionCS` has nothing to difference. The eight UAVs that takes is exactly
the D3D11 feature-level-11.0 limit, so anything added there has to displace something.

Three things follow. `prev_position_map` needs `D3D11_BIND_UNORDERED_ACCESS` like
`position_map` (both stay RTVs in `DrawScene`'s seven-target set; the flag only adds a
second way to bind them, and the two are never bound at once). The previous position
comes from a single `inverse(world) * prev_world` matrix constant rather than a per-splat
previous position — every splat of a cloud shares one transform and there is no skinning,
so it is exact and keeps `SplatView` at 60 bytes instead of growing 12 more times 1.8M.
And `splat_material` carries `RAY_TRACING_ENABLED_FLAG`: the rasterizer clears every
texture-map bit but keeps that one, because it is what decides whether the ray sources it
writes are followed or skipped.

It sits after both `DrawScene` calls and before `ProcessMotion`, and both halves matter:
the preprocess rejects splats behind the depth pre-pass result, and the mixer downstream
builds the frame out of the buffers the rasterizer writes into. The scene colour it
composites into is `post_process_pipeline->RenderUAV()` — the same texture `DrawScene`
bound as an RTV — so the pass does nothing when no post-process chain is installed, and
it can only run once the render targets are unbound.

**There is no per-tile capacity, and every previous attempt to have one failed the same
way.** A tile's entries occupy a contiguous slice of one pool, at an offset the two scan
passes hand it, so a tile takes exactly the room it needs. `splat_info` reports what the
frame used.

The history is worth keeping, because all three versions rendered a *plausible* cloud and
the defect was only visible when magnified, or by binning image gradients on x mod 16:

- **Fixed capacity, append, drop the excess.** The dropped share differed per tile and
  changed every frame. Measured on a 1.8M splat capture: tile-boundary gradient 3.8x the
  local gradient, 1.3% of the viewport flickering.
- **Fixed capacity, `InterlockedMin` of a packed (depth, index) key into `ticket % cap`.**
  Better — the survivors are biased near — but this is *not* "keep the nearest N", it is N
  samples each biased near, drawn from groups the atomics formed. Raising capacity 1024 ->
  4096 (33 -> 133 MB) only moved the ratio 3.8x -> 2.6x, and covering the worst tile
  (45,665 entries) would have needed **780 MB**, since the buffer is tiles x capacity.
- **A per-tile *cull*** — reject anything more than a window behind the tile's nearest
  splat — which looked like a way to fit under capacity and was itself a per-tile
  threshold, so it stepped at every tile boundary too. It also cost ~20% of real coverage.
  Removing it took the ratio to **0.88x** (i.e. gone) with frames **bit-identical**.

**The pass would otherwise cost the same at 100 units as at 2, and the reason is a
one-line trap.** `SplatPreprocessCS` looks like it culls sub-pixel splats
(`if (radius < 0.33f)`), and that test can never fire: the low-pass floor a few lines
above adds `0.3` to both diagonal terms of the 2D covariance, so the smallest radius any
splat can project to is `3·√0.4 ≈ 1.9 px` at any distance. Distance therefore never
removes splats, it only *concentrates* them — measured on the 1.8M capture, `total_binned`
stayed near 3.4M from 2 units to 100 while `tiles_used` fell 255 → 4, so one tile held
1.24M entries and four thread groups did all the work to produce 197 pixels. 183 ms of a
200 ms frame.

`Components::SplatCloud::max_density` fixes it: the most splats worth drawing per screen
pixel (16 by default). `DrawSplats` turns it into a keep probability against the cloud's
projected area and `SplatPreprocessCS` drops the rest before loading anything, by a
threshold on `SplatHash01(idx)` — the splat index and nothing else, so the subset is
stable frame to frame (a frame term would make the cloud boil) and raising the density
only ever *adds* splats to the set already drawn.

| distance | before | after |
| --- | --- | --- |
| 2 | 22.2 ms | 20.4 ms (identical binning — the probability clamps to 1) |
| 15 | 111.1 ms | 16.67 ms (vsync) |
| 40 | 200 ms | 16.67 ms (vsync) |

Two things about that knob. It is a *density*, so one number does both jobs — it thins a
receding cloud, and it caps an over-dense capture at full size, which is how to optimise
a model carrying more splats than its silhouette can show. And it must stay well above 1:
the rasterizer *averages* albedo and normal so a random subset has the same mean, but
coverage scales with the fraction kept, and too low a value stops `saturate(acc_w)`
pinning a covered pixel's alpha to 1 — the cloud goes translucent rather than merely
coarser. Around 4 is where that starts to show.

A note for anyone testing it: the knob only binds where the cloud is *over* its density,
so at a normal viewing distance every setting clamps to 1 and drops nothing. That is the
correct answer and an untestable one — `23-splatrender` pushes the fixture cloud to 200
units to get it into the range where the probability bites.

**The binning IS a parallel counting sort by depth, and `SPLAT_DEPTH_BUCKETS` is its
precision.** Histogram, prefix scan, scatter — that is a counting sort already, so making
the slice finer means widening the radix digit, never bolting a comparison sort onto the
end. At **1024** buckets over a span of `SPLAT_MAX_DEPTH_STEP` (1023) the bucket index
*equals* the quantized depth, so the sort is exact and two entries sharing a bucket share
a depth. Going finer means more `SPLAT_DEPTH_BITS`, not more buckets, and the two have to
move together or the 1:1 property silently degrades to an approximation. It costs
`tiles * 1024` uints of histogram — 7.1 MB at 1080p, 57 MB at the 2560x1377 the editor
runs at, cleared per cloud per frame — and pins `SplatScanCS` at the 1024-thread D3D11
group cap, that dispatch being one thread per bucket.

That exactness is what lets `SplatRasterCS` composite front to back with **transmittance**
(`w = a * T`, `T *= (1 - a)`, stop when `T < SPLAT_MIN_T`) instead of summing
order-independently. Occlusion between splats then falls out on its own, so nothing
decides where "the surface" ends: the slab, the band width, the tile near-depths and the
crossing test are all gone from that shader, and `surface_alpha` does one job, gating
whether the cloud owns the pixel's G-buffer. It still accumulates *material* — a 3DGS
renderer composites baked colour, this one fills a G-buffer and lets the engine light it.

**Co-located splats must be composited as ONE layer, and this is the trap.** "They are at
the same depth so the order cannot matter" is false: alpha compositing is order-dependent
even between co-located splats, since `c1*a1 + c2*a2*(1-a1)` is not the same as swapping
them unless the colours or the alphas agree. A naive walk therefore boils on exactly the
residual the sort cannot remove. Measured on a 60k fixture, frozen clock and fixed camera,
consecutive frames: **0.002% of pixels differing became 0.42%, worst delta 21/255 became
100** — the same magnitude as the 0.17%, 0.51% and 1.3% flickers every earlier version of
this pass was rewritten to remove. So each run of equal-depth entries is gathered and
composited once, as `sum(c*a)/sum(a)` and `1 - prod(1-a)`, both symmetric. That returns it
to 0.002%. `SplatWalk` in the shader is that two-level state.

Two smaller things from the same work. `SPLAT_MAX_ALPHA` is not cosmetic — `alpha` carries
`opacity_scale`, which is unbounded above, and an `a` over 1 makes `(1 - a)` negative,
which flips the sign of everything behind it rather than merely brightening a pixel. And
the layer flush divides by `max(sum(a), 1e-20)` because **fxc flattens that branch** (it
warns X4008), so the divide runs for an empty layer too and `0/0` puts a NaN into an
accumulator that survives every later add.

**`entries_walked` in `splat_info` is the only evidence the early-out works**, because a
walk that stops at the opaque surface and one that reads every entry produce the same
image by construction. With `SPLAT_EARLY_OUT 0` it is exactly `total_binned * 256`; with it
on, the 60k fixture walks 11.5% of that at 2 units and 15.7% at 6. The two are not
bit-identical — the difference is bounded by `SPLAT_MIN_T`, so check the distribution
(13.85% of pixels at >= 1/255, 0.0003% at >= 48) rather than the max.

Seven things there that are not guessable:

- **Every result in the rasterizer used to be order-independent, deliberately** — see the
  transmittance note above for why it no longer is, and what had to be true first. The
  history is still worth knowing: `surface_depth` was once "the entry at which a running
  coverage sum crossed `surface_alpha`", which moved every frame and dragged the
  reconstructed world position, the shadow lookup and `depth_map` with it.
- **The depth buckets must span the cloud's whole quantized range.** Narrowing them looks
  free — finer bands exactly where the slab lives — but everything past the span clamps
  into the last band as an unordered mass, and the early-out is only sound where the
  ordering is real. At a 0.1 span that put the tile ratio back to 1.68x and returned 0.17%
  flicker to a frame that was otherwise bit-identical. Spanning the range costs little: on
  a cloud ~1 unit deep, 32 bands are ~0.03 against a 0.05 slab.
- **The count and scatter passes are one shader with a flag** (`bin_pass`), not two
  shaders. The first's histogram is the second's allocation, so a single pair enumerated
  or bucketed differently writes into the neighbouring bucket's entries.
- **Bindings do not survive an intervening dispatch.** D3D11 binding slots belong to the
  *stage*; `SimpleShader`'s per-object API hides that. `SplatBaseCS` binds `tile_total` at
  its `t0` and nulls it on cleanup — the same slot `SplatBinCS` holds `splat_views` in — so
  the scatter read `splat_views` as null, every splat took the `radius == 0` reject path,
  and the pool was never written. The frame came out empty while the counting pass still
  reported correct totals. **Re-bind everything before every dispatch.**
- **The pool readback is load-bearing, not diagnostic.** `total_binned` is what sizes the
  pool, so unlike the radiance cache counters it is *not* keepalive-gated. Gated, the pool
  only grew while a tool happened to be watching, and the automation fixture's cloud
  rendered at 2.6% of its coverage. Entries per splat is not near 1 and cannot be a
  multiplier: it is ~120 for a small cloud close up (large ellipses, many tiles each) and
  under 2 for a 1.8M capture at a normal distance, so `SPLAT_POOL_MIN_ENTRIES` is a floor
  and the measurement does the sizing.
- **The depth quantization window is the cloud's bounding *sphere*, fitted per frame,
  from `world_xmmatrix` and never `world_matrix`.** The stored matrix is *transposed* (the
  convention shaders read `world` under), and `XMVector3TransformCoord` wants the
  untransposed form — loading the wrong one still produces *a* number, so the fit silently
  described nothing. It measured a 1.015-unit-deep cloud as 0.081, which saturated the
  quantization and made both the ordering and the cull compare noise. Not the camera's
  clip planes either (1023 steps over 0.01..1000 puts a whole cloud in one step), and not
  the min/max over the eight box corners — the nearest point of a box to a camera outside
  it is generally on a *face*, so that fit is too tight, and over-tight is the only
  failure mode that matters.
- **The rasterizer may not early-out of the batch loop.** Every thread has to keep
  reaching `GroupMemoryBarrierWithGroupSync`, so the *work* is gated on a `done` flag and
  the control flow is not — fxc rejects the alternative outright (X4026). `SPLAT_EARLY_OUT
  0` disables both early-outs and must produce an identical image; it is how to separate
  "the walk stops too soon" from "the binning is wrong", and it has earned its keep twice.

**A 3DGS `.ply` is not in the engine's frame, and `SplatCloudData::Load` converts it.**
The reference trainer works in COLMAP's convention — right-handed, X right, **Y down**,
Z forward — and this engine is left-handed Y-up, so negating Y fixes the axis and the
handedness in one step. Loaded raw a capture comes out upside down *and* mirrored, which
reads as broken geometry rather than a frame mismatch, and rotating the entity 180° is
not the same fix (it corrects the axis and leaves the mirror). The flip reaches three
things and missing any one is silent: the position, the minor-axis normal, and the
covariance as `Σ' = DΣD` with `D = diag(1,-1,1)` — which negates `Sxy` and `Syz`, and
leaves the diagonal and `Sxz` alone. `BuildDefault` is authored in engine space and is
not converted.

**`splat_info` is the only way to see any of this.** A cloud that is over-binned, short of
pool, or not rasterized at all renders a plausible surface either way. It reports
`tiles_used`, `max_per_tile`, `total_binned`, `dropped` (non-zero means the pool was
short), `capacity`, and — the pair that separates "not on screen" from "not rasterized" —
`tiles_rastered` and `pixels_written`. It is a few frames stale, so poll it.

`Core::RWStructuredBuffer` and `Core::RWTypedBuffer` (`Core/RWBuffer.h`) exist for this
pass and are the non-raw counterparts of `RWByteBuffer`.

**A `StructuredBuffer` bound through `SimpleShader` used to take the process down, and
the reason is worth knowing before adding another one.** `shaderDesc.ConstantBuffers` is
*not* the number of cbuffers: reflection reports one entry per constant-buffer-shaped
thing, and every `StructuredBuffer`/`RWStructuredBuffer` contributes one of type
`D3D_CT_RESOURCE_BIND_INFO` describing its element layout (the "Resource bind info for
&lt;name&gt;" block in `fxc /dumpbin`). Those are not buffers to create: their `Size` is the
element stride, `CreateBuffer` rejects it as a constant buffer size, and the slot is left
holding a null that `CopyAllBufferData` then hands to `UpdateSubresource`. The crash is an
access violation *inside D3D*, on a stack pointing at whatever pass happened to be
drawing — nothing about it says "structured buffer". `ISimpleShader::LoadShaderBlob` now
skips anything that is not `D3D_CT_CBUFFER`/`D3D_CT_TBUFFER` and recomputes
`constantBufferCount` from what it kept. Nothing hit this before because the BVH and
vertex buffers are bound by explicit register and never reach that loop.

**`Transform::world_matrix` has exactly one owner per entity, and a splat cloud needs a
system of its own to get one.** It is written in three places now:
`StaticMeshSystem::Update` (needs `Mesh` *and* `Bounds`), `PhysicsSystem::Update` (needs a
rigid body), and `SplatCloudSystem::Update` for everything carrying a `SplatCloud` that
neither of those claims. Only `Base` and `Transform` are mandatory components and
`TemplateOps` builds a cloud template out of `SplatCloud` plus an identity `Transform`, so
without that third system a cloud entity's matrix is never composed at all — and a zero
matrix sends every splat to the origin with `w = 0`, which renders as a cloud welded to
the world origin rather than as an error. The exclusions are mutual and are what keeps two
background threads from composing one matrix from different inputs.

A trap for anything testing this: `TemplatePanel::IsMandatory` makes `Mesh`, `Material`
and `Bounds` mandatory on a **template**, so a template built from a `.ply` carries a
stand-in cube and every placed cloud arrives wearing one. That cube sits exactly where the
cloud is *and* hands the entity back to `StaticMeshSystem` — so a transform test written
against a freshly placed instance passes whether `SplatCloudSystem` exists or not.
`23-splatrender` strips it (`Remove-StandInMesh`) and asserts it is gone.

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

**The `ray_*` views show the ray tracing *source* targets — where the rays to run are
stored.** `rt_ray_sources0/1` is the pair every tracing pass reads before it traces
anything: a `RaySource` per pixel, world position and normal in the two `xyz` halves
(which `position`/`normal` have always shown) with four material scalars packed as fixed
point into the two `w` channels by `ShaderStructs.hlsli`'s `getColor0`/`getColor1`.
`ray_dispersion`/`ray_reflex`/`ray_density`/`ray_opacity` show those four on one
cold-to-hot ramp, and `ray_sources` shows the tracers' own accept/reject decision as one
colour per class. It is the view that separates "this material does not reflect" from
"nothing wrote a ray source here" — the frame renders the two identically, and the mask
is decoded through `fromColor` and gated by the same predicates as `RayTraceCS`
(reflections/refractions, which additionally needs `dispersion` in `[0,1)`) and
`GIRayTraceCS` (indirect, which does not), so a class disagreeing with what a tracer
does is a bug in one of the three.

Three things there that are not guessable:

- **A decoded 1.0 is not `<= 1.0`.** `fromColor` divides by `1000.0f`, fxc rewrites that
  as a multiply by the reciprocal, and `0.001f` is not exactly 1/1000 — so a value stored
  as exactly 1.0 decodes to 1.00000005. Tested against a bare `> 1.0f`, the out-of-range
  flag fired on *every* opacity and density in a normal scene (both default to 1.0 and
  are by far the commonest values either takes), which reads as the whole scene being
  broken rather than as a boundary nit. `RAY_SCALAR_TOP_SLACK` is the tolerance, chosen
  above that error and below the packing's own 0.001 step. The tracers compare the same
  decoded value against the same `1.0f`, so their behaviour is unchanged and the mask
  deliberately mirrors it rather than correcting it.
- **These four honour `debug_gain`** — the exception the `motion` view already was.
  Their ranges differ per scalar, so there is no single natural display range: dispersion,
  reflex and opacity read directly at gain 1, while density is an index of refraction
  starting at 1.0 and wants ~0.5.
- **The demo scene's terrain is amber, and that is correct.** `Dirt`, `Grass` and `Floor`
  have `specular: 0.0`, so `dispersion = saturate(1 - spec)` is 1.0 and the reflection
  tracer rejects them while ReSTIR still gathers from them. Its sky *dome* is ordinary
  geometry drawn by `MainRenderPS`, so it writes ray sources and is classified too —
  `SkyPS` declares the two-target `RenderTarget`, so only where no dome covers the
  background does a ray view come out black.

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

**GI convergence lives in a world-space hash grid, not in the pixel that saw it**
(`Shaders/Common/RadianceCache.hlsli`, resolved by `Radiance/RadianceCacheResolveCS.hlsl`).
The ReSTIR pass keeps its state in two screen-space places — the per-pixel pdf and the
denoised history — and both die when the camera moves: the pdf is not even reprojected,
it is blended toward flat in proportion to how far the pixel moved, because a pdf
describing a *different* surface point is worse than none. So a rotation restarts every
pixel at the 1–2 rays a frame the pass can afford, and a disoccluded surface starts from
nothing. The cache is keyed by *where the surface is*, so it survives both.

It costs no rays. `GIRayTraceCS` deposits the estimate it already computed into the cell
its pixel's surface point falls in; filling the cache is a side effect of work the frame
was doing anyway.

Six things there that are not guessable:

- **It stores linear irradiance, deposited *before* the `pow(color, 0.5)` at the end of
  the trace.** That square root is a display convention of the screen-space path (the
  mixer uses its output directly); the cache is read back as incoming light for a bounce,
  where a square root is meaningless. Confusing the two makes the cache look empty — at
  gain 4 a correctly-filled cache still renders near-black, because it holds the *square*
  of what the `indirect` buffer shows. Check it at `debug_gain 64` before concluding
  anything is broken.
- **Two buffers, and the split is the point.** `rcache` is the atomically updated part
  (key, this frame's accumulator, age); `rcache_value` is the resolved value, written
  only by the resolve pass, so a lookup is one 16-byte load that never contends with the
  deposits. Both are raw `ByteAddressBuffer`s — SM5 only guarantees the interlocked ops
  there, not on a structured buffer whose element is a struct — so the offsets are
  spelled out by hand and **nothing checks the stride**.
- **An evicted cell becomes a tombstone, never free.** Zeroing the key would cut the
  probe run and orphan every cell that had probed past that slot, which reads as a cache
  that inexplicably stops answering for one region of the level.
- **`RC_MAX_AGE` is the memory the cache exists for.** 64 frames (~1 s) expires a surface
  in about the time it takes to look away from it — exactly the case this is meant to
  fix. It is 512.
- **The tracer reads and writes the cache through its UAV, not an SRV.** `GIRayTraceCS`
  is at the 128 texture-register limit (see the `DiffuseTextures` note), and a Buffer SRV
  costs a `t` register where a UAV costs one of eight `u` registers.
- **`Base::is_static` is not consulted yet.** Dynamic geometry drags stale radiance
  through the cells it vacates; the `RC_MIN_BLEND` floor bounds how long that takes to
  re-converge (~20 frames) rather than preventing it.

**The cache buys multi-bounce for one buffer load, and `RC_BOUNCE_GAIN` is a loop gain
rather than a brightness knob.** `GetColor` adds the cell's cached irradiance to the
light arriving at a hit, *before* the albedo multiply — on the far side it would double
the albedo and darken every second bounce. The value read is last frame's, since only
the resolve pass writes `rcache_value`, so light advances one cell per frame like a
radiosity relaxation and there is no read/write race with the deposits. It also partly
defeats `max_distance`: a ray reaches 10 units, but the cell it lands on already holds
light that arrived there from further away.

It is a feedback loop — a cell's value is fed back into the rays that fill it — so the
gain must stay under 1 or a white material holds energy instead of losing it and a
corner brightens without bound. 0.9 costs a few percent at the second bounce and less
at each one after.

Measured on **sponza opened in the Scene Editor** (Marbles solo level 3 loads fine via
`--level`, which is the way to get the editor's frozen clock and `gi_cache_info` onto a
scene that actually has bounced light): frozen clock and fixed camera give a within-build
floor of meanDelta 0.33–0.43 with *zero* pixels over threshold, against which the bounce
is meanDelta **8.78 with 48% of pixels changed** — indirect mean 32.3 → 39.9 (**+23%**),
final frame **+26%**. The demo scene is useless for this: it is outdoors, so GI rays
mostly hit sky and never reach the lookup at all, and an A/B there returns the floor
whatever the gain.

Two surfaces can see it, because a cache that is working and a cache that is absent
render identically: `gi_cache_info` (occupancy, deposits, drops — `dropped` is the one
number that says `RC_ENTRIES`/`RC_PROBES` are too small for the scene) and the
`gi_cache` / `gi_cache_conf` debug buffers. The value view paints **blue where the
lookup found no cell**, so "no cell" is distinguishable from "a black cell"; the
confidence view is black for no cell and a cold-to-hot ramp otherwise, which is the
difference between the cache not working and the cache not having got there yet.

**`SimpleShader` binds buffer SRVs by name now, and that was a silent failure before.**
Its reflection registered only `D3D_SIT_TEXTURE`, so
`SetShaderResourceView("<a ByteAddressBuffer>", srv)` looked the name up, found nothing,
returned false — and no caller checks that return. The buffer stayed unbound and every
read of it returned zero, which is why the BVH and vertex buffers are bound by explicit
register through `CSSetShaderResources` instead. `D3D_SIT_BYTEADDRESS` and
`D3D_SIT_STRUCTURED` are registered too now; the explicit binds still work and were left
alone.

**A non-blocking GPU readback needs far more ring depth than it looks like it does, and
should not run every frame.** `Core::RWByteBuffer::Readback` copies to a staging ring and
maps a slot several frames old with `DO_NOT_WAIT`. At three slots (two frames of slack)
the map failed essentially *every* call once the driver was buffering frames ahead — it
succeeded once during startup and then never again, so the counters froze at their first
reading and looked exactly like a cache that had stopped filling. It is eight deep, it
scans oldest-first for a slot that maps, and it tracks which slots hold a copy at all
(mapping a never-written staging buffer succeeds and returns zeros — "an empty cache"
rather than "no reading"). It also runs **only while something is asking**:
`GetRadianceCacheStats` sets a keepalive, because a `CopyResource` plus a driver `Map`
on the render thread every frame, for counters nothing in the frame depends on, is a
real cost paid by every build.

**The cache also fills in at the primary pixel, and the gain there is real but small.**
`GIAverageCS` pass 3 - the one place that already knows whether temporal reprojection
succeeded - blends the cell's value into the result, weighted by how *little* the pixel
can rely on its screen-space history: a pixel whose reprojection landed nowhere usable
takes the cache outright, and one with good history takes at most `RC_PRIMARY_BLEND`,
because there the screen estimate is the sharper of the two and carries contact detail a
cell several centimetres across cannot. Confidence gates it, or an unresolved (black)
cell would darken a disocclusion instead of filling it. **The value must be
`sqrt()`-encoded on the way in** - the cache is linear, this pass and the mixer are not.

Measured on sponza (rotate away, settle, rotate back, capture the recovery frame by
frame against the settled reference): the improvement is **~8%, consistently, from the
fourth frame on** - not the step change the design predicted. The reason is worth
recording: the existing path already recovers in about four frames, because
`GIAverageCS`'s spatial kernel is enormous (43 taps) and its motion-driven temporal
blend reaches 0.8, so a disoccluded pixel is filled by its neighbours almost at once.
The "twenty frames of visible convergence" this stage was aimed at is not what the
screen-space path actually does on that scene. `RC_PRIMARY_ENABLE` is the master switch
that A/B exists for - `RC_PRIMARY_BLEND` is not, since a pixel with no history ignores
it by design.

**A hash grid read once per pixel draws its own cells on screen, and the fix is two
things, not one.** A cell is constant across its volume, so one lookup per pixel is a
piecewise-constant image - literally cubes. They are worst while the camera moves,
because that is when the primary-pixel fill leans hardest on the cache.

- **Jitter the lookup** (`RCLookupJittered`): displace the point by up to half a cell
  before quantizing, with a seed that varies per pixel *and* per frame. A pixel near a
  boundary then lands in either neighbour in proportion to how close it is, so the
  average over pixels is the trilinear blend of the surrounding cells and the residual
  is noise - which the denoiser and the temporal accumulation already exist to remove.
  A step edge is not, and no amount of blurring stops an edge reading as an edge.
  `RCLookup` (unjittered) is kept for the `gi_cache` debug view, which is *supposed* to
  show the cells.
- **Make the cells small enough that the dither is fine-grained.** This is the half
  that is easy to miss. The first sizing (`RC_BASE_SIZE` 0.25, `RC_LEVEL_SCALE` 0.1)
  held a cell at a constant ~24 full-resolution pixels - on sponza a cell was as wide
  as the column it was shading, and jitter alone would only have turned 24-pixel blocks
  into 24-pixel blotches. 0.125 / 0.05 gives ~6 pixels; sponza goes 1.5k -> 6.4k live
  cells, which is 1.2% of `RC_ENTRIES`, so there is room to go finer still.

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

**Anything drawn outside the render trees needs its own line in that latch.** `drawables`
is built from the trees, so a splat cloud — no `Mesh`, no `Material`, drawn by
`DrawSplats` — is not in it, and its `prev_world_matrix` kept the zero it was constructed
with. That is worse than a stale pose: `SplatRasterCS` reconstructs the previous position
through `inverse(world) * prev_world`, so a zero matrix sends every point to `w = 0`, and
the cloud reported **full-strength motion while standing still** (100% of its pixels
deflected in the `motion` view; 0.1% after the fix). Every temporal pass downstream then
reprojects it to nowhere. `LatchPreviousFrame` walks `splat_clouds` too; an entity
carrying both a Mesh and a SplatCloud is latched by both loops, which is the same
assignment twice.

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

**A level can also be generated, and then it is a mesh asset like any other.**
`Core::SimplifyMesh` (`Core/MeshSimplify.h`) reduces a mesh by quadric edge collapse;
`World::GenerateMeshLod` runs it, registers the result under `<source>_lod<n>`, and
records the `{name, source, ratio, file}` recipe in `generated_meshes` — a level
section that `World::Load` reads *after* the meshes and *before* any template is
created, since a template's chain may name one. The editor surface is the Components
panel's percentage + **Generate level** (`MeshOps::GenerateLod`, `generate_lod
[<percent>]`); it always simplifies from level 0, never from the level above, because
reducing a reduction compounds the error and a level's `ratio` is its share of the
*full* mesh.

Four things there that are not guessable:

- **The geometry is cached, the recipe is the truth.** `Assets/GeneratedMeshes/*.hbmesh`
  holds the vertices so a load is a read rather than a simplification, and the file
  carries the source's name and vertex/index counts plus `sizeof(Vertex)`: anything
  that no longer matches (a re-exported model, another build, a truncated write) is
  discarded and rebuilt. So the folder is safe to delete and there is nothing to
  invalidate by hand — and a stale file can never draw yesterday's model as today's
  level.
- **A surviving vertex is always one of the two the edge had.** The optimal point a
  quadric solves for is a better fit and is what an offline tool uses, but it is a
  position no vertex ever had, so its UVs, tangents and above all its four bone indices
  and weights would have to be invented. Keeping an original vertex is what makes a
  generated level pass `CompatibleSkinning` against a rig that is animating.
- **Vertices are welded by position for the collapse, and only for it.** The importer
  clones a control point per polygon corner, so the index buffer alone describes a
  surface in pieces and an edge collapse on it tears seams open. The corners ride along
  and are re-picked per triangle (nearest UV), which is also why the reduction is
  measured in *corners*: they get shared as triangles merge, so the vertex count falls
  faster than the triangle count and a target counted in welded positions overshoots by
  a third.
- **Boundaries and UV seams are weighted, not locked** (`BORDER_WEIGHT` 100,
  `SEAM_PENALTY` 32). Locking them keeps every reduction exact and stops a shell or a
  heavily seamed model — which is most game models — from reaching its target at all.

The mesh asset and its file are deliberately outside undo, exactly like File/Import
Model: undo takes the level back off the chain, and leaves the asset registered (which
is what lets redo name it again). `SetMeshSmooth` reaches a mesh's generated levels
too, or a model would change shading the moment it dropped one.

**The ray tracers trace a level of detail too, and not the one being drawn.**
`PrepareRT` points every object's three geometry offsets at **the coarsest level its
mesh has** and hands that one `ObjectInfo` array to reflections, refractions and
ReSTIR indirect alike. Not the level being drawn, and not something the ray tracing
quality moves: this pass is the most expensive thing in a ray traced frame (see the
cost table below), and what it produces is a half-resolution, denoised, temporally
accumulated image, so a silhouette in a reflection is the cheapest thing in the frame
to be approximate about. A mesh with no chain is traced at full detail, that being the
coarsest level it has.

It was briefly two arrays - reflections on the drawn level with the quality as a
floor, only the GI on the coarsest - and that is worth knowing only so nobody rebuilds
it: it bought a difference nobody can see and cost a second 20 KB cbuffer upload per
frame. `rt_quality` moves `RT_TEXTURE_RESOLUTION_DIVIDER` instead, which is where its
cost is (divider 1 vs 3 is 27 vs 52 fps on sponza).

A generated level already carries its own BVH (`MeshData::Init`) at its own
`bvhOffset`, so nothing else was needed to make this work. `LodGeometry` answers an
out-of-range level with the *full* mesh, so the index handed to it is clamped to the
end of the chain rather than trusted.

**None of it is visible in a screenshot**, which is why `rt_info` exists: a ray hitting
the wrong geometry still produces a plausible reflection. It reports `full_indices`
(what level 0 would have cost) against `traced_indices` (what the rays walk), summed
over the objects sent. On sponza from the atrium that is 291447 against 78468.

**The traversal is a single-fetch descent, and every part of that is load-bearing.**
`RayFunctions.hlsli`'s `aabb_entry` returns where along the ray it enters a box, or
FLT_MAX, and the per-object loop in both tracers uses it for all three questions a
descent asks - visit this child at all, which child first, and can this child still
hold anything nearer than the best hit. Four things changed together there and each is
worth keeping:

- **A node is fetched once.** The old loop pushed both children and popped one, so
  every internal node was read three times over: once as each parent's child (to
  order it) and again off the stack. Now the nearer child is stepped into directly
  and only the far one is pushed.
- **`1/dir` is computed per ray, not per box.** `IntersectAABB` divided on every call.
- **The ordering key is the ray's entry distance**, not the distance to the box's
  bounding *sphere* (the old `node_distance`), which is always shorter than the real
  one - it ordered children by a point the ray may never pass through and culled less
  than it could.
- **`IntersectionResult` carries indices, not positions.** The three vertex positions
  it used to hold are 9 floats on every one of three live structs, in shaders already
  at the cs_5_0 register limit; `bary_position` reloads them once per hit instead.

Measured on Marbles' sponza, alternating the two `.cso` sets on one binary: **45 → 55
fps** at the shipped quality and **27 → 39** at full RT resolution, with the frame
bit-identical.

**`max_distance` is not a limit along the ray, and treating it as one is a trap worth
one paragraph.** It is how far from the *pixel* the tracer looks at all - the same
distance-from-origin test `BuildCandidateList` applies to whole objects - so the node
gate has to compare it against the distance from the ray *origin* to the box, which is
what `node_distance` is still there for. Folding it into the entry-distance test looks
like the same thing and is not: the boxes of a big wall are entered 20+ units along a
ray whose origin sits well inside `max_distance` of them, and rejecting those took
**80% of the indirect light** out of sponza while leaving the final frame
bit-identical (the raw `indirect` buffer is the only place it showed). Two lessons:
gate the two questions separately, and check the raw GI buffer - `render debug_buffer
indirect gi_denoise 0` - against a same-build capture, which on a fixed camera is
bit-reproducible, so any difference at all is real.

**Where a ray traced frame's time actually goes**, measured by switching features off
one at a time and reading the game's own counter - each toggle is a dispatch genuinely
not submitted. Marbles' sponza (level 3), 1920x1080, before the traversal work:

| off | fps | so that block costs |
| --- | --- | --- |
| nothing (baseline) | 27 | 37.0 ms total |
| `gi_denoise 0` | 29 | ~2.5 ms (`GIAverageCS`) |
| `indirect 0` | 32 | ~5.5 ms (all of `ProcessGI`) |
| `rt_denoise 0` | +3 fps on top | ~3.5 ms (`DenoiserCS`) |
| reflection **and** refraction (skips `ProcessRT`) | 74 | **~23.5 ms** |
| that plus `indirect 0` | 120 | leaves ~8.3 ms of everything else |

Neither the reflection flag nor the refraction flag alone changes anything - they are
shader flags on one dispatch that runs if *either* is set, and only the C++ skips the
block when both are clear. It is GPU cost, not submission: the CPU spends 0.05 ms in
`ProcessRT`.

**Do not read the RenderDoc profiling table as a frame budget, and do not read it as an
attribution either.** On that frame it charges `GIAverageCS` 25.0 ms and `RayTraceCS`
1.0 ms - the ladder above says 2.5 and ~20. Use the table to rank passes and the ladder
for magnitudes. Two traps while measuring: the *game's* `render rt_quality` takes a
**number** (0-3), not the editor's names, and a rejected command still answers - `ERR`
scrolls past and the reading looks like "this setting does nothing" (it cost an hour
here). Check the response, and confirm the divider actually moved.

**The top-level BVH (`USE_OBH`) is off, and that was measured rather than assumed.**
Turning it on is *correct* - the two variants render the same frame to within 1/255 -
and at a wide viewpoint it is faster (sponza from up the atrium: `GIRayTraceCS`
2.20 -> 1.68 ms). At the player's viewpoint it is **slower**: 26 -> 24 fps,
reproducibly, and the loss is entirely the GI tracer. The reason is
`BuildCandidateList`, which applies `max_distance` **once per pixel** and in an
interior rejects almost every object for a few ALU ops; the hierarchy re-derives that
rejection once per *ray*, with dependent 32-byte node loads and a second indexable
stack. Sizing the volume stack to the object count (`MAX_VOLUME_STACK_SIZE`) recovers
about a third of the loss, not the rest. Turn it on if the object count grows well past
`MAX_OBJECTS` = 100, or the ray budget grows long enough that the distance cull stops
rejecting.

Two things found while measuring that stayed in, both independent of that switch:
`tbvh_buffer.Refresh` now also runs in `ProcessGI` (it was only in `ProcessRT`, which
does not run when reflections and refractions are both off - so a GI pass walking the
hierarchy would have read one built for an earlier frame, or never written at all), and
`PrepareRT` returns early on an empty object list (`TBVH::Subdivide` recurses forever on
a zero-count root).

**Marbles' sponza level carries generated levels** (`../Marbles`, solo level 3): the 116
meshes of 1000 indices or more - 91% of the level's geometry - each got a 35% and a 10%
level, 232 generated meshes cached under `Assets/GeneratedMeshes/`. The chains are there
for the ray tracers, which trace the 10% level: from the atrium that is 78468 traced
indices against 291447 at full detail. The *raster* saving from the same chains is small
(`MainRenderPS` 3.00 -> 2.77 ms), and at the player's viewpoint the chains move the
frame rate not at all - the frame there is the ray tracing pass, not the triangles.

**`Transform::dirty` is cleared by whichever system consumes it first, so no system
may rely on it alone.** `CameraSystem::Update` and `StaticMeshSystem::Update` both
clear it, and they run on *different background timers* — while a camera rig is very
often a mesh entity as well, because every template carries a Mesh and Bounds and a
rig placed from one therefore does too. When the mesh timer won the race, the camera
never recomputed: the commanded position sat in the Transform, the view matrix kept
the old one, and nothing would ever dirty it again. The pose was not applied late, it
was dropped for the rest of the session — the editor's camera silently stops
responding to `camera_pos`, `focus` or a gizmo move, and only sometimes, which is why
it read as flakiness in `19-multimaterials` and `20-lods` rather than as a bug.
`Components::Camera` now keeps `last_position`/`last_direction`/`last_rotation` and
recomputes when those differ, so the shared flag is an optimisation for it rather
than its only signal. Anything else that grows a second consumer of `dirty` needs the
same treatment; `StaticMeshSystem` already had it for the parent pose and the
animation box.

**A commanded camera pose is not in effect on the next frame**, and four of `20-lods`'
switching tests were intermittently reading the level from *before* the move because of
it (roughly one run in fifteen). `EditorCamera` applies the pose on its own tick, so
`Set-CameraDistance` in that suite now polls the `camera` readout until the rig is
actually at the requested distance before returning. Any new test that moves the camera
and then asserts on something the render decides needs the same treatment; anything that
needs a particular level *drawn* can also wait for it (`Wait-Lod`) or change the rule
instead of the camera - `lod_mode: distance` with a switch point of 0 makes the coarse
level eligible everywhere, with no camera involved. `lod_bias` is not a substitute for
that: it is clamped at 0.05.

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

**An entity can also come from nothing, and then the level has to record it itself.**
Add/Entity (`EntityOps::CreateEmptyEntity`; the menu entry is `Add/Entity`, so it is
scriptable like every other) creates an entity carrying only `Base` and `Transform` —
the two the component registry marks `Mandatory` — and selects it, to be told what it is
one component at a time in the Components panel. It is the third way to get an entity,
next to placing a template and pasting one, and the only one that belongs to no asset: a
marker, a trigger volume, a spawn point, anything whose whole content is a game's own
component. It lands at the middle of the view like a placed template, which is where it
will appear once it has a Mesh.

Three things follow from "belongs to no asset":

- **Its existence is data of its own.** Nothing else in the file implies it, so the level
  carries a `created_entities` array — name, live pose and every component block, written
  whole from `EditorState::created_entities` exactly like `instances`. A created name is
  therefore kept *out* of the `entities` override array, where it would be a second,
  partial copy of the same thing. `World::Load` rebuilds them before the `entities` phase,
  so an override entry (or a wildcard rule) reaches one just as it reaches an FBX-authored
  entity, and before `clones` so one can be a clone source.
- **It is deletable whatever it carries.** What makes a scene entity deletable is
  Base+Transform+Bounds+Mesh, which an empty one does not meet until the user gives it
  those, and refusing would make Add/Entity a one-way door. It parks like any other entity
  (`EntityOps`' `ParkInfo`), except that what park and unpark drop and restore is its
  `created_entities` record: there is no authored name for `removed_entities` to name, the
  entity never having been in the file it is being removed from.
- **A hand-assembled entity reaches states no importer produces**, and `World::Init` had a
  latent throw for one of them: it read `Bounds::local_box` off anything carrying a Mesh,
  and `GetComponent` on a component that is not there *throws* rather than returning null.
  Mesh-without-Bounds is only reachable this way, and the throw is out of an `Init()`
  nothing catches, on the *next* load — so the level that saved would not reopen. It now
  requires both components, a collider having nothing to be sized from without the box.

**A model's name is a registry key, not the file name** — it defaults to the file stem
and File/Import Model... now asks for it before loading anything, because it is the key
the assets are filed under and renaming afterwards would mean re-registering them. A
name that is not the stem is written to the level as `"name"` beside `"file"`, and such
a model earns its `models` entry even when nothing uses it yet (the rule that a model
must be *named by* something to be listed has nowhere else to recover the name from).
The Asset Browser's folder scan therefore matches an already-listed file **by path** as
well as by name, and runs after the level's own models are listed — otherwise the same
`.fbx` is adopted a second time under its stem, loaded twice, and duplicated in the
array on every save. `Remove` (`remove_model`) unregisters a model and drops it from the
level; its meshes and materials stay loaded for the session, because they live in the
same flat collections every other model points into (the reason `RemoveMaterial`
retires rather than erases), and the file is left on disk. Not undoable, like the import.

Fixed while doing this, and worth knowing because it was silent: `World::LoadModel`'s
`.ply` branch **ignored `relative`**, so a level's own splat-cloud entry resolved
nothing and registered an *empty* cloud. In the editor the folder scan then loaded the
same file again under its stem and that copy is what everything used, which hid it
completely; a game, having no folder scan, simply got no cloud.

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
