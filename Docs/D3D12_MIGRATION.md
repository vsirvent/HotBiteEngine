# Direct3D 11 -> Direct3D 12 migration (running log)

Direct, in-place port. No RHI, no second backend, no D3D11 fallback: `RenderSystem` and
`SimpleShader` are already the graphics interface and no layer goes over them. Games and the other
tools are allowed to be broken while it is in progress; the Scene Editor is the test vehicle.
Shaders move from fxc `*_5_0` to DXC `*_6_x`.

Started 2026-10-15 on branch `no_SSE`. Committed once the Scene Editor and Survival suites passed on D3D12
(see "State at the commit"). Performance work (M7) is deliberately left for later; one known problem (DXC
against fxc on the lighting shader) is described under "Performance".

## Design in one paragraph

D3D12 has no immediate context, so `DXCore::context` becomes a concrete, non-virtual
`Core::GfxContext` wrapping the frame's `ID3D12GraphicsCommandList` and keeping the method names the
engine already calls (`OMSetRenderTargets`, `DrawIndexed`, `Dispatch`, ...). It holds the D3D11-style
"current state" (VS/HS/DS/GS/PS, rasterizer/blend/depth, topology, input layout, RT/DS formats) and
resolves it at `Draw`/`Dispatch` into a cached PSO, copies the staged descriptors into the
shader-visible heap, and emits pending barriers. Resources track their own `D3D12_RESOURCE_STATES`.
Root signature: per-stage CBV/SRV/sampler tables for VS/HS/DS/GS/PS plus a compute set with a UAV
table, so HLSL register numbers stay as they are. Constants go through a per-frame upload ring; GPU
object releases go through a deferred-release queue; 2-3 frames in flight on a fence. Minimum:
feature level 12_0, SM6.0, resource binding tier 2.

## Milestones

| | what | state |
|---|---|---|
| M0 | prerequisites, baseline numbers, this log | done |
| M1 | device, flip swap chain, heaps, upload ring, `GfxContext`; editor on `imgui_impl_dx12` | done |
| M2 | textures, buffers, readback, texture loading | done |
| M3 | shaders: reflection, root signature, `SimpleShader` staging, PSOs | done |
| M3b | DXC / SM6.0 for every shader (`Dxc.targets`, `DxcSupport`, `ShaderCompiler`) | done |
| M4 | passes in dependency order (GUI, sky, depth, shadows, scene, lighting, post, RT/GI, splats, particles) | done |
| M5 | editor extras; text in labels | done (labels: Direct2D on a WIC bitmap, uploaded, see below) |
| M6 | Scene Editor + Survival suites, other hosts (Survival, DemoGame, Marbles, tools) fixed, docs, commit | done |
| M7 | GPU frame-time work (re-scoped 2026-10-15, see below), measured against the baseline | in progress: async compute built and measured (off by default, "Async compute"); lighting shader on DXC and the rest open |

## Where the code is

New: `Engine/Core/GfxDevice.{h,cpp}` (objects with their own resource state, views, state objects, shaders,
queries, the device, immediate uploads, deferred release), `GfxContext.{h,cpp}` (the immediate-context-shaped
recorder: current state resolved at Draw/Dispatch into PSOs, descriptor tables, barriers; rings for
descriptors and uploads), `QuadBatch.h` (what the GUI used DirectXTK's PrimitiveBatch for). Ported:
`DXCore` (device, flip swap chain, a fixed-address back-buffer texture copied to the swap chain at Present,
state objects as descriptions), `Texture`, `RWBuffer`, `SimpleShader`, `Vertex`, `BVH`, `PostProcess`
(`ScreenDraw` no longer uses `PrimitiveBatch`), `Material`/`TextureCache` (textures decode through
DirectXTex and upload through `GfxDevice`), `TiledLights`, `SplatCloud`, `RenderSystem`, the GUI headers,
and the Scene Editor (`ImGuiDx12.{h,cpp}`, `imgui_impl_dx12` vendored from ImGui v1.90.9, previews, mask
paint, texture panel). Also new: `DxcSupport.{h,cpp}` (dynamic load of `dxcompiler.dll`, reflection) and
`Engine/Dxc.targets` (the MSBuild step that compiles shaders with `dxc.exe`).

Games reach the GPU only through the engine (`DynamicTexture2D`, `DXCore::SaveBackBuffer`, the widgets):
Survival's `FogRender`, `Minimap` and `ScreenCapture` no longer include a D3D header.

### Text in labels (the Direct2D question)

Direct2D cannot draw into a D3D12 resource. `Label` keeps DirectWrite for layout and now has
`Direct2D::RenderText`, which draws the text with Direct2D onto a **WIC bitmap on the CPU** (straight RGBA),
and `DynamicTexture2D` uploads it; the label draws that texture as a quad after the widget. The texture is
created once per label and re-uploaded only when the text or size changes (re-creating it every frame cost
~25% of the frame: 4 labels, about 78 x 21 texels each, with blocking uploads). No D3D11On12 device is used.

## Deviations from the plan

- **Shaders were brought up on fxc `*_5_0` DXBC first**, because D3D12 accepts DXBC and a wrong picture can
  then not be a DXC difference. M3b then moved every shader to DXC (below). A DXBC shader still *loads* (the
  runtime tells them apart by the `DXIL` container part), so a single compute shader can be swapped for an
  fxc build to compare; **a PSO cannot mix SM5 and SM6 stages** (creation fails), so that only works across
  different PSOs (compute shaders, or all graphics stages of one PSO together).
- **No `d3dx12.h` and no DirectXTK12.** The D3D12 structures are filled in by hand; DirectXTK's D3D11
  renderers are no longer used, only its header-only `SimpleMath.h` / `VertexTypes.h`.
- **One use of Direct3D 11 stays**, in `TextureCache.cpp`: DirectXTex's GPU BC7 compressor takes an
  `ID3D11Device`, and cooking a texture is 100x faster with it than on the CPU. The renderer never touches
  that device.
- **Builds go to `Solution/x64/D3D12Run/`**, not `Release/`, so an editor or game that is open from
  `Release/` is never overwritten while the port is in progress.

## Goal re-scoped after the baseline (2026-10-15)

The goal started as lower CPU / draw-call overhead. The D3D11 baseline below shows the render CPU laps
are only ~1.5 ms of a 9.7-13.2 ms GPU-bound frame, so the user re-scoped M7 to **GPU frame time**. M1-M6
(the port itself) are unchanged. M7 candidates, in the order the baseline points at: deferred lighting
(~5.1 ms of the 8.2 ms scene stamp in the default view) and the scene pass; explicit barriers and UAV
overlap instead of D3D11's implicit serialisation between the many compute passes; async compute for
the independent ones (tile light culling, RT/GI, shadow accumulation, splat preprocess); `ExecuteIndirect`
where counts already come from the GPU (ray lists, splat tiles); fewer full-screen passes in post.
Every candidate is judged by the GPU stamps and fps against the table below with the user's settings.
CPU-side work (per-material descriptor tables, multithreaded recording) stays on the list only if the
CPU laps grow once the port is in.

## Shaders: DXC, SM6.0 (M3b)

- **Build.** `Engine/Dxc.targets` is imported at the end of `Engine.vcxproj`, `DemoGame.vcxproj` and
  Survival's `Survival.vcxproj`. Projects keep listing shaders as `FxCompile` items (same `ShaderType` and
  `AdditionalIncludeDirectories` metadata); target `HotBiteDxcCompile` (before `FxCompile`) takes them over and
  runs `dxc.exe -T <vs|hs|ds|gs|ps|cs>_6_0 -E main -HV 2018 -Ges -no-warnings -I ...` into `$(OutDir)<name>.cso`
  when the `.cso` is missing or older. Headers are still not tracked: `InvalidateShadersOnSharedHeaderChange`
  (the engine sets `HotBiteDxcDependsOn` to it) touches the dependents. `HotBiteCopyDxc` copies
  `dxcompiler.dll` + `dxil.dll` beside every executable. Model: `HotBiteDxcModel` (6_0).
- **Run time.** `ShaderCompiler` (hot reload, the editor's shader panel) compiles with `IDxcCompiler3` and the
  same flags (`HOTBITE_SHADER_MODEL` in `ShaderCompiler.h` names the profile suffix; Debug adds `-Zi -Od`).
  `DxcSupport::Reflect` reflects a DXIL blob with DXC's `CreateReflection` and anything else with `D3DReflect`.
- **Reflection patch.** DXC's reflection reports wrong register numbers for implicit-register resources when the
  shader also declares explicit ones. `DxcSupport` wraps the reflector and takes BindPoint / Space from the
  container's `PSV0` part (the signed runtime info), which is right. Without it the wrong SRVs get bound.
- **Array names.** fxc named an array resource `name[0]`, DXC names it `name`. `SimpleShader` registers both
  (shadow cube arrays, multi-material arrays, `DiffuseTextures`, `hiz_textures`, `ssHiZ`).
- **Traps found** (also in memory `hotbite_dxc_vs_fxc_traps`): a function-scope accumulator that is never
  initialised was zero in fxc and garbage in DXC (`matrix m = (matrix)0;` in `DepthVS` / `ShadowVS` - the
  skinned troll turned the whole frame into garbage and made suites flicker); a global `float` without
  `static const` becomes a constant-buffer variable (`Utils.hlsli` `Epsilon`); `[unroll]` on a loop with a
  runtime bound is an error in DXC (`[loop]`); `texture2D` lower-case is rejected; array-of-struct casts
  crashed DXC (an internal compiler error on a `memcpy` bitcast), so `MultiTexture.hlsli` unpacks with an
  explicit `MULTI_UNPACK8(p)` macro; `21-shaders.tests.ps1` matches `error`, not fxc's `error X`.

## Shader symbols and PIX

Every shader is built with debug symbols (`-Zi`), so PIX shows a shader's HLSL source (the file and its includes), its
line table and variable names, and can attribute a draw's or dispatch's cost to source lines. The symbols are a part
of the container beside the code, not in it: the `DXIL` part is byte for byte what a build without symbols makes
(checked on `DeferredLightRaysCS`: 37,040 bytes either way), so what PIX profiles is the shader that ships.

`HotBiteShaderDebug` (MSBuild property, set it on the command line or in the project; default `embed`):

| value | what it does |
|---|---|
| `embed` | the symbols (and the source text) are inside each `.cso` (`ILDB` part). PIX needs no setup. The `.cso` grows (`DeferredLightRaysCS`: 44 KB to 381 KB; the 74 engine shaders together are 7 MB) |
| `pdb` | the `.cso` keeps its size and `<hash>.pdb` files go to `$(HotBiteShaderPdbDir)` (default `<OutDir>ShaderPdb\`, must end in a backslash). Add that folder to PIX's *Tools > Options > Shader PDB search paths* |
| `none` | no symbols (a Debug configuration still gets `-Zi`, it is built with `-Od`) |

Changing the mode, or any DXC flag, rebuilds every shader of that project: `Dxc.targets` keeps the flags in
`<OutDir>hotbite-dxc.flags` and deletes the project's `.cso` when they differ. The editor's hot reload and a new
shader made from the material panel compile with `-Zi` as well (`ShaderCompiler`); `HOTBITE_SHADER_DEBUG=0`
leaves it off there. To profile: start `Survival.exe` (or the editor) from PIX, take a GPU capture, open an event
and its shader; the GPU timings and counters are per draw / dispatch, and with the symbols they are per source line
where the GPU exposes it.

## Debug and diagnostics (env vars)

| variable | effect |
|---|---|
| `HOTBITE_D3D12_DEBUG=1` | D3D12 debug layer (always on in Debug builds); `HOTBITE_D3D12_GBV=1` adds GPU-based validation |
| `HOTBITE_PIX=1` | loads PIX's GPU capturer (`WinPixGpuCapturer.dll` of the newest PIX under `Program Files\Microsoft PIX`) before the device is created, so **PIX can attach to the running process** (Attach tab) and take a GPU capture; the log says which file. The two queues are named ("HotBite direct queue", "HotBite compute queue") and the frame's passes are markers (the `GpuStamp` sites: shadows, depth pre-pass, scene, motion, reflections, GI, lighting, mix, AA, blur, tile culling, deferred lighting). `GfxDevice::PixCaptureNextFrames(file, frames)` (`RenderSystem::PixCapture`) asks the capturer for a GPU capture of the next frames and it writes the `.wpix` itself, with no PIX window (Survival's `pix_capture` channel command). Nothing is loaded or recorded without it. Leave the debug layer off for timings (and GPU based validation off for a capture); `pixtool attach` is for timing captures, a GPU capture of a process PIX did not launch needs this |
| `HOTBITE_LIGHT_SPLIT=0\|1`, `HOTBITE_LIGHT_COMPILER=dxc\|fxc\|auto` | the lighting shader as permutations or as the one shader (default 0), and who built the ones that run (default `auto`); see "Lighting permutations" |
| `HOTBITE_LOG=<file>` | engine log written to that file from `DXCore::InitDirectX` (device, adapter, frame cost every 240 frames, PSO compiles >= 5 ms) |
| `HOTBITE_SHADER_DEBUG=0` | the shaders the editor compiles at run time (reload, new shader) leave the debug symbols out |
| `HOTBITE_GFX_TRACE=<serial>` | in the log, every draw and dispatch of 3 frames from that frame serial, with what each had bound |
| `HOTBITE_GFX_TRACE_CREATE=1` | traces resource / view creation |
| `HOTBITE_DUMP_DIR=<dir>` (+ `HOTBITE_DUMP_FRAME`) | once, at frame 200: the frame's main targets, the post chain and the back buffer as PNGs in that folder |

The renderer logs `gfx cpu per frame` every 240 frames: the CPU time waiting on the GPU, and the immediate
uploads (an immediate command list per upload, a ring of 16 in `GfxDevice`) that block.

## Performance (user's settings, `Measure-Baseline.ps1`, fps cap lifted)

**Known problem, left open for later: DXC compiles the ray traced lighting shader 2-2.5x slower than fxc.** The same
HLSL of the ray traced lighting costs 22.0 ms built by DXC (`cs_6_0`) and 8.7 ms built by fxc (`cs_5_0 /O3`) in the
stress scene below; the rest of the frame is not affected. **For now the lighting shader that runs (`DeferredLightAllCS` and
`AllRaysCS`, the shader as it was) is built by fxc** (the `*FxcCS.hlsl` twins): `HotBiteFxc` metadata on their `FxCompile` items in
`Engine.vcxproj` (read by `Dxc.targets`, which runs `fxc.exe /O3` on them), and `UsesFxc` in `ShaderCompiler.cpp` for the
editor's hot reload (a reload of a rays shader takes ~90 s at /O3, on a worker thread). They are compute shaders, so each is a
pipeline of its own and mixing SM5 and SM6 is fine (a single pipeline cannot mix them). Cutting the shader into permutations (next
section but one, "Lighting permutations") did not close the gap. To go back to DXC once it is fixed: set `HOTBITE_LIGHT_COMPILER=dxc`
as the default (`RenderSystem::lighting_compiler`) and drop the twins and the list. Where to start: PIX on the DXC build of suite `67-rt-shadow-stress` (the symbols are in the `.cso`), compare
the machine code and occupancy against the fxc build, look at how DXC lays out the BVH walk and the screen march
(loops, `[branch]`, local arrays and structs, the `PackedLight` loads), and try other options on a copy (shader model
6_6, `-Gis`, `-enable-16bit-types`, `-Vd`).

| build | default view fps | deferred stamp | forest fps | deferred stamp |
|---|---|---|---|---|
| D3D11 (before the port) | 75.7 | 5.082 ms | 103.4 | 1.599 ms |
| D3D12, 1:1, fxc DXBC | 78.3 | 4.889 ms | 106.0 | 1.487 ms |
| D3D12, DXC DXIL everywhere | 49.3 | 11.786 ms | 95.3 | 1.945 ms |
| **D3D12, DXIL except the two lighting shaders on fxc (as committed)** | **80.7** | **3.996 ms** | **102.9** | **1.272 ms** |

The 1:1 DXBC port is as fast as D3D11 (slightly faster); **moving everything to DXIL cost 37% in the default view**,
all of it in `DeferredLightCS` (with the pair back on fxc the port is faster than D3D11: 80.7 against 75.7 fps): Survival suite 57 reads the deferred pass at 13 ms (DXIL) against 5.4 ms
(DXBC), and with the ray traced shadow code stubbed out the same shader takes 1.2 ms. So it is the
ray-tracing branch (RayScreenSolver / ssHiZ / ScreenShadow / ShadowRays) that DXC compiles worse
(occupancy; the register / live-range cost of the BVH walk plus the screen march). Tried without effect:
`-O0..-O3`, `-all-resources-bound`. **Two builds of the shader** (`DeferredLightCS` with no rays, and
`DeferredLightRaysCS` = the same file with `DEFERRED_LIGHT_RAYS 1`) so a frame that traces nothing never
runs the heavy one (stubbing the ray code out took the lighting pass of Survival's suite 57 scene from 13 to 1.2 ms).
`RenderSystem::ProcessDeferredLighting` picks the build per dispatch (`world_shadows || screen_shadows`).
That does not recover the default view, which traces. **Tried and dropped:** a second dispatch that lit only
the ray traced lights with the rays build (and the rest with the cheap one): no faster, and the lamps came
out 31% brighter with traced shadows than without (Survival suite 63), so the traced lights stay in one dispatch.

Survival suite 57 (`57-tiled-lights`) has cost limits (3 / 5 ms, and culling must beat brute force 3x). Its two
cost tests failed on D3D11 HEAD (5.5-5.9 ms) and still do (5.9 ms) with the lighting shaders on fxc; with DXC the
pass cost 14.5 ms and the culling-ratio test failed too (43 ms brute force against 14.8 culled).

**Same source, two compilers (2026-10-15, RTX 4050 Laptop, driver 32.0.15.8142).** Survival suite `67-rt-shadow-stress`
(ten houses and ten street lamps at midnight, 25 traced lights, the busiest tile holds 15, ~2 M (pixel, traced light)
pairs on screen) run against one executable, changing only how the two lighting compute shaders are compiled
(GPU ms per frame, `perf` stamps, 4 s after a 2 s settle):

| lighting shaders built by | deferred, traced shadows on | scene | fps | deferred, traced shadows off |
|---|---|---|---|---|
| DXC `cs_6_0` (as shipped; run twice: 22.01 / 22.02) | **22.0 ms** | 26.0 ms | 32 | 0.72 ms |
| fxc `cs_5_0 /O1` (run twice: 10.84 / 10.86) | 10.8 ms | 14.9 ms | 50 | 0.70 ms |
| fxc `cs_5_0 /O3` | **8.7 ms** | 12.7 ms | 56 | 0.70 ms |
| fxc `/O1`, only `DeferredLightRaysCS` swapped | 10.9 ms | 15.0 ms | 50 | 0.72 ms |

(The shipped state is the third row.) So it is the shader, not the engine: the same HLSL runs 2.0-2.5x slower once DXC compiles it, the whole gap is in
`DeferredLightRaysCS` (swapping only it gives the same time as swapping both), and the build without ray tracing
is fine in every case (0.7 ms). The run to repeat it: `Run-Tests.ps1 -Exe <exe> -Suite '67-*'` prints the
`rt stress cost` line; to compile the pair with fxc use `fxc /T cs_5_0 /E main /O3 /Fo <exe folder>\<name>.cso
Engine\Engine\Core\Shaders\ComputeShaders\<name>.hlsl` for `DeferredLightCS` and `DeferredLightRaysCS`. The
`DXIL` part is the same size with or without `-Zi`, so PIX shows the same code the run above timed.

## Lighting permutations (`DeferredLightCS.hlsl`)

The idea (2026-10-15): the lighting shader holds the BVH walk and the screen march of the shadow rays, whose registers every
pixel pays for whatever it is, and DXC builds that code at half the speed of fxc. So cut the shader into passes that each hold only
what their pixels need, and see what DXC and fxc make of the pieces. **Built, tested, measured; not faster, so off by default.**

**What it is.** One HLSL file, four permutations, each dispatched over the same tiles and adding what it owes to the light and bloom maps
(`RenderSystem::ProcessDeferredLighting`):

| permutation (`.cso`) | pixels | lights |
|---|---|---|
| `DeferredLightCS` (nothing defined) | ordinary surfaces (standard, splats) | ambient, every directional light and every point light whose shadow is a map or none |
| `DeferredLightRaysCS` (`DEFERRED_LIGHT_RAYS`) | the same | only the lights flagged ray traced, with the ray tracer compiled in; `DL_RT_LIGHTS_ONLY` leaves out of it the cascades, the PCF, the cube maps and the cascade debug tint, which a light that traces never uses |
| `DeferredLightWaterCS` (`DEFERRED_LIGHT_WATER`) | water | its own ambient, directional and point light |
| `DeferredLightLavaCS` (`DEFERRED_LIGHT_LAVA`) | lava | directional and point light with shadow maps |

A light is lit by exactly one pass: `skip_rt_lights` makes the first permutation leave the ray traced lights to the second, which is
dispatched only when the frame has such lights and a place to trace them (`world_shadows || screen_shadows`); until then the first lights
them unshadowed, as the shader without rays always did. (The earlier try at splitting the lights, which came out 31% brighter, double counted
them; Survival's suite 63 is the check.) Water and lava are dispatched only when the geometry pass drew some: `DrawScene` notes a visible
entity whose pixel shader is `WaterPS.cso` / `LavaPS.cso` (the engine's two shaders that write those shading models; a game's own shader that
wrote them would not be lit). The reference, `DeferredLightAllCS.hlsl` (+ `AllRays`), is the shader as it was and what runs by default.
Every one of the six is built twice: by DXC, and by fxc as the `*FxcCS.hlsl` twin that includes it (`HotBiteFxc` metadata in `Engine.vcxproj`,
`UsesFxc` in `ShaderCompiler.cpp`; `Engine.vcxproj` touches wrappers and twins when the file they include is newer).

**Switches.** `lighting_shaders [split|single] [dxc|fxc|auto]` (Survival's channel and the Scene Editor's; answers the dispatches of each permutation in
the last frame), `HOTBITE_LIGHT_SPLIT=0|1`, `HOTBITE_LIGHT_COMPILER=dxc|fxc|auto`. `auto`, the default, is DXC where it is as fast as fxc
(the permutations that trace no ray) and fxc where it is not (the one that does, and the reference). `gpu_timings` / `perf` report each
permutation (`light_standard`, `light_rt`, `light_water`, `light_lava`).

**Same picture** (the checks): Survival suite 67 shoots the stress scene with the reference and the permutations, each built by either compiler, and with
the reference twice for the noise (0.4-0.6 levels of 255 against 0.4-0.5 for every pair); the Scene Editor's new suite `50-lightingpermutations` does it for
the ordinary surfaces, the water and the lava (the floor's material given `WaterPS` / `LavaPS`, over a floor, with a check that the scene is not black) and the
ray traced point lights and sun, and that a permutation that has nothing to light is not dispatched. Suites 44, 46, 47, 49 (and 63) cover the lighting under
the permutations too. (`mouse_move 0.99 0.99` before a shot: a real mouse over a house lights it up in the picture and made the noise 6.9 levels.)

**What it costs** (Survival suite 67 `lighting ...` lines; RTX 4050 Laptop, GPU ms per frame of the first lighting pass, `deferred` stamp, between
runs of the other sets; 25 ray traced lamps, the busiest tile holds 15; the pass is `rt` where the permutations are split):

| built by | one shader | permutations: standard + rays + water | permutations, no traced shadows |
|---|---|---|---|
| DXC | 22.1-23.1 | 0.8 + **17.1-21.3** + 0.13 = 18.2-22.4 (three runs; the shader file of the last two is the same) | 0.74 (one shader 0.73) |
| fxc | **9.4-9.6** | 1.0 + **8.4-8.7** + 0.13 = 9.6-9.9 | 0.69 (one shader 0.70) |
| auto (standard and water on DXC, rays on fxc) | - | 0.82 + 8.78 + 0.13 = **10.0** | 0.745 |

Static: DXIL instructions 2481 (one shader) -> 1525 (standard permutation), 4521 -> 2786 (rays, after leaving out the shadow map code; 3211 before), water 408,
lava 755; fxc `dcl_temps` 19 -> 20, 41 -> 41, 15, 10 and instruction slots 6702 -> 4587, 12302 -> 8285.

What it says:
* **The registers are the ray tracer's.** The rays permutation keeps the 41 temps of the whole shader under fxc and is the whole cost (8.4 of 9.6 ms); trimming
  the shadow map code out of it saved 8% under DXC and nothing under fxc. The other permutations are small and cheap (0.8 ms, 0.13).
* **DXC gains 3-18% from the split and is still 2x behind fxc** on the rays permutation (17-21 ms against 8.4-8.7); the DXC figure moved by 25% between
  runs of the same file where the one-shader figure moved by 5%. Under fxc the split is neutral to a little slower: the standard permutation costs
  0.8-1.0 ms that, in the one shader, hid in the idle time of the ray walk, and the water pass costs 0.13 ms.
* **DXC is as fast or faster where there is no ray**: standard 0.79 against 0.94-1.0 ms under fxc, water and lava equal.
* **The water pass costs 0.13 ms when a water surface is in view even if no pixel is water**: Survival's water is one mesh under the whole map, visible
  wherever the terrain is, and the dispatch cannot know that no pixel shows it (the check is by entity, on the CPU). A frame with no ray traced lights
  is 0.745 ms with the permutations and 0.70 with the one shader.
So the **default stays the one shader, built by fxc** (`lighting_shaders single auto`), which is what shipped before, and the permutations are for the day the
ray code is better in DXC. Where to go from here: the BVH walk is two nested loops with two stacks (`SrTrace` over the top level, `SrObjectOccluded` in each
object) and DXC inlines the second into the first; one loop over one stack of (node, object) entries is the usual way to cut the registers and the live
ranges of a traversal, and is the place to try first; after it the screen march (`RayScreenSolver.hlsli`) and the world walk can be permutations of their own
(`shadow_space` is world, screen or both). `HOTBITE_LIGHT_COMPILER` and the twins stay so that the next change is measured the same way.

**Next (M7), by expected payoff:** (1) find why DXC is slow on the lighting shader and put it back on DXC (see the
known problem above and "Lighting permutations": the traversal is where to start), and compare the other big shaders (`GIRayTraceCS`, `RayTraceCS`, `DenoiserCS`) DXBC against
DXIL the same way - the lighting pair on fxc is the stopgap, restructuring the ray code (tile classification,
packed ray lists as `feedback_pack_rays_for_warps`) the other road; (2) explicit barriers / UAV
overlap instead of the full serialisation between the compute passes; (3) ~~async compute for the
independent ones~~ built and measured, a loss except for the reflections beside GI (see "Async compute"); what would pay is
overlapping frames; (4) fewer full-screen post passes.

## Async compute (the compute queue)

Every compute dispatch has an `async` argument: `GfxContext::Dispatch(x, y, z, async)`, `DispatchIndirect(args, offset, async)`,
`SimpleComputeShader::DispatchByGroups/DispatchByThreads(..., async)` and `TiledLights::Cull(..., async)`. A flagged dispatch is
recorded on a second command list that runs on a COMPUTE queue (`GfxDevice::ComputeQueue`) beside the graphics work that follows it.
**It is off by default** (`RenderSystem::SetAsyncCompute`, `HOTBITE_ASYNC_COMPUTE=1`, the `async_compute` automation command),
because what it buys on this renderer is nothing measurable and, flagged everywhere, a loss (below); nothing about the picture
changes either way.

**How it works.** A frame is a sequence of *segments*: a command list on one queue that starts after some waits and signals a value
of its queue's sync fence when it ends. The engine still records in the order it issues commands; the compute list is a second list
that is open beside the direct one. What orders the queues is the resources' own tracking, which already existed for barriers:

- each resource remembers which queue wrote it last and which queues read it since, as the sync-fence value of the segment
  (`GfxResource::last_write_queue/value`, `last_read_value`); a use on the other queue that conflicts (a read after a write, a write
  after anything, **and every state transition, which counts as a write**) makes its queue wait for that segment. The segment is closed,
  so it can be signalled, and the waiting queue starts a new list after the wait (`GfxContext::MakeWait`, `Touch`);
- a compute list can only move a resource between compute states (UAV, non-pixel shader resource, copy, indirect argument). A resource
  in a render-target, depth or pixel-shader state is moved on the direct list, ahead of the point the compute queue is made to wait for,
  once per dispatch (`direct_sync_pending`). Compute shaders read their textures in the non-pixel state (also on the direct queue), so a
  texture only compute reads is never in a state the compute queue cannot leave;
- everything a draw or dispatch uses is moved to its state **before any command of it is recorded** (`ResolveGraphicsUses`,
  `ResolveComputeUses`), because that is where a list may be closed to wait; if it was, the draw starts again on the open list
  (`cut_counter`). A frame executes its segments in the order they were closed (every wait refers to a segment closed before the one
  that waits, so there is no cycle), the direct queue ends the frame waiting for the last compute segment, and the frame fence is
  signalled after that: a frame serial still means the whole frame is done, so deferred release, readbacks and the rings are unchanged.
  The compute queue also waits for the immediate uploads made so far (a new resource with contents).

What is flagged (`RenderSystem::AsyncPass`, a bit each, `async_compute passes <list>`, `HOTBITE_ASYNC_PASSES`): `autofocus`, `hiz`,
`motion`, `volumetric`, `gi`, `rt`, `tilecull`. Motion vectors and GI read only the G-buffer, so when no later pass rewrites it (no
second geometry pass, no splat clouds) they are recorded right after the first one and run beside the deferred lighting; the
reflections and GI have their own tiles texture, so one of them can go to the queue while the other runs on the direct one. The
reflections go to the queue only while GI runs too (`AsyncReflections`): that is the chain they overlap with. Default passes: `rt`
(when it is turned on). `HOTBITE_ASYNC_PASSES=<list>` sets the passes of a process (to run a suite with every one of them).

**Measured** (RTX 4050 Laptop, driver 32.0.15.8142, Survival suite `67-rt-shadow-stress`: ten houses and ten street lamps at
midnight, 25 traced lights, the user's settings, fps cap 60; every configuration between runs with async off and compared with their
average, because the laptop GPU drifts by several percent over a minute; `off` runs spread by 1-3% in the runs below):

| settings | passes on the compute queue | against off | queue work per frame |
|---|---|---|---|
| traced shadows, as played (55.5 fps) | all | **-3.6%** | 10 dispatches, 11 lists, 7 waits |
| | hiz + autofocus | -1.3% | 4 dispatches |
| | volumetric (3 dispatches) | -1.0% | |
| | tilecull | -2.2% | |
| | motion vectors | -0.1% | |
| + GI low (46.4 fps) | all | -3.5% | 16 dispatches, 13 lists, 8 waits |
| | gi | -1.2% | |
| + GI low + reflections low (42.7 fps) | all | -3.3% | 24 dispatches, 22 lists, 14 waits |
| | gi | -1.3% | |
| | rt (the default) | +2.5%; three more runs: +1.1%, -0.1%, +3.9% | 8 dispatches, 9 lists, 6 waits |
| | gi + rt (both heavy chains on the queue, the lighting pass on the direct one) | -0.5% | 14 dispatches, 12 lists, 8 waits |

The last two rows are the same measurement in different runs, and that is the point: the GPU slowed during the session (the `off`
configuration fell from 56 to 45 fps across the runs of the stress scene; the laptop throttles) and `off` runs of one configuration
spread by up to 8%, so a difference of a few percent is not a result. The reflections on the queue come out between -0.1% and
+3.9%: **no reliable gain anywhere; the one consistent number is the loss of flagging everything (-3% to -5% in every run)**.
**The experiment the question "GI and the reflections on the queue, the lighting pass on the direct queue" asks for** is limited by
the water: GI can only start once the G-buffer is final, i.e. after the second geometry pass. Recording it right after the first pass
instead (an experiment, with the water then lit as the sea bed: the picture changes there) gave `gi + rt` +3.6% and `rt` alone +3.9%
in the same run: letting GI overlap the 4-9 ms lighting pass added nothing to the noise.

In the Scene Editor's demo scene (GI, reflections and refractions on; the GPU span from the stamps, since the editor's frame rate is
capped by something else) the reflections on the queue cost +0.15 ms (2%): the gain is not general. The picture does not change: two
shots with it off differ by 0.76 levels (0-255) over the sampled pixels, the default passes by 0.78, every pass by 0.82 (the suite checks
it). The debug layer reports nothing with every pass on the queue (default view, and with GI and reflections on).

**Why it does not pay here.** (1) A wait between the queues costs ~30-50 us, 2-25 happen a frame (the passes that can move have to
wait for the direct queue to finish the G-buffer or the depth, and the direct queue for them before the mix), against passes of
0.1-0.3 ms to hide. (2) What would hide them is work the direct queue has to do meanwhile and that does not depend on them, and the
deferred lighting, the one big pass of the frame, only has the tiny ones to overlap with. (3) **Survival's water is a second geometry
pass that rewrites the G-buffer after the main lighting**, so GI and motion cannot start before it (they would light the sea bed
instead of the water) and never overlap the 4-9 ms lighting pass. (4) Two heavy kernels that run at the same time are not faster than
the same two in a row on this GPU: reflections beside GI measured 1.94 ms against 1.68 ms in sequence in the editor's scene (the direct
queue's kernel slows down), so the gain exists only where the partner has latency to hide (the BVH walks). With hardware-accelerated
GPU scheduling unreported on this machine, a different GPU or driver may do better: the switch and the per-pass bits are there to measure.

**What would pay** (not done): overlapping frame N's lighting and post-processing (compute) with frame N+1's shadow maps, depth and
G-buffer (raster, ~5 ms), which are independent of it. That needs the per-frame targets double-buffered (position, normals, material,
light, depth, the post chain's textures: tens of MB), the present of frame N delayed until frame N+1's first segment is submitted (the
queue cannot start N+1's raster behind a wait for N's tail), and one more frame of latency.

**Also tried in the same change (not async), and reverted:** the G-buffer pass not writing depth for what the depth pre-pass already
drew (`DrawScene` with the test-only state per material, still writing for alpha-tested and blended materials, entities with
`draw_depth` off, materials with no depth shader and the whole second pass, i.e. the water). Default view 80.7 -> 81.5 fps, forest
102.9 -> 101.7 (the scene stamp -0.13 ms): inside the noise. And the Scene Editor's suites 18 (`denoiser bypass`), 46 and 47 (the ray
traced shadow against the shadow map's) stopped passing with async compute off: the shadow map's picture of the test scene lost its
shadow (mean difference from the unshadowed picture 0.26 levels). Nothing reads `depth_view` after the geometry pass, so the cause is
inside the pass (surfaces within the pre-pass's depth bias, 100 + 1.0 slope, now resolve by draw order); not investigated further.
Restoring the writes brought the three suites back. If it is tried again, those three are the check.

## State at the commit (2026-10-15)

Scene Editor suite on D3D12: **733 passed, 1 failed, 3 skipped**. The one failure is
`48-screenspacert` "the screen finds the reflection of the boxes the world finds" (the world solver's
reflection reads black), which fails the same way on D3D11 HEAD and with every graphics/compute shader in
fxc DXBC, so it is not the port. (HEAD had 713 passed / 20 failed: the gicache, tessellation and tiledlights
failures it carried pass now. `35-tessellation` has three "the view is not settled" screenshot checks that failed
once inside one full sweep and pass alone, 36/36.)

Survival suite: **306 passed, 16 failed**. The sixteen are HEAD's (302 passed / 16 failed there), none new:
02 (a string literal in `FpsHud.cpp`), 27 (`fan01` has no NightLight), 39 (`street_lamp` is free), 48 and 49
(`--cook` exits 1, a building material names a texture under Marbles), 57 (two cost tests, 5.9 ms against limits
of 3 and 5), 65 and 66 (lamp emission removed). The four tests of the new stress suite 67 pass. Suite 08's
two-process determinism test now runs the two processes one after the other (a second New Game while the first
held its textures ran out of memory on a loaded machine; same code on HEAD).

Builds: Engine, SceneEditor, Survival, DemoGame, UiDesigner, MaterialDesigner, HotBiteTool and Marbles build;
Marbles / DemoGame start and run. Not run: Marbles' suite (this checkout has none that applies), the DemoGame and
tool UIs beyond a start-up check.

## M0 findings

### Toolchain on this machine
- Windows SDK 10.0.26100.0 has `d3d12.h`, `d3d12shader.h`, `d3d11on12.h`, `dxcapi.h`, and in
  `bin\10.0.26100.0\x64` `dxc.exe`, `dxcompiler.dll`, `dxil.dll`. **No NuGet package is needed for DXC**;
  the two DLLs have to be copied next to each executable (engine post-build, `Survival.vcxproj`).
- **`d3dx12.h` is not in the SDK.** Needs `Microsoft.Direct3D.D3D12` (or DirectX-Headers) from NuGet.
- DirectXTK is `directxtk_desktop_2019` (DX11); M2 needs `directxtk12_desktop_2019`.
- ImGui is **1.90.9**; the tree has `imgui_impl_dx11` and `imgui_impl_win32` only, so
  `imgui_impl_dx12.{h,cpp}` from the v1.90.9 tag has to be vendored in M1.
- No `nuget.exe` on PATH (only `dotnet`); restore goes through VS's MSBuild.

### Surface that has to change
- ~12 engine files (`DXCore`, `SimpleShader` 2.1k lines, `Texture`, `RWBuffer.h`, `PostProcess`,
  `TiledLights`, `BVH.h`, `Vertex.h`, `Material`, `Lights`, `GUI`, `RenderSystem.cpp` 5.6k lines),
  ~256 context calls, ~516 `ID3D11*` uses.
- Raw call counts outside `SimpleShader`: `OMSetRenderTargets` 21, `RSSetState` 12, `OMSetBlendState`
  8, `OMSetDepthStencilState` 5, `IASet*` 12, indexed draws ~10, `Dispatch` 62 (mostly through
  `SimpleComputeShader::DispatchByGroups`), `ClearUAV` 3, `CopyResource` 5, `UpdateSubresource` 5,
  queries ~14. `SetShader(` has 138 uses. Only ~40 raw `*SSetShaderResources/ConstantBuffers/
  Samplers` calls, 21 of them in `SimpleShader.cpp`.
- Stream-out is never used (delete `useStreamOut` from `SimpleGeometryShader`).
- `Label` text draws with Direct2D on a DXGI surface (`Direct2D::GetD2DRenderTargetView`).
- `ID3D11*` is in the public headers (`Interfaces.h`, `Texture.h`, `SimpleShader.h`), so Survival
  (`FogRender`, `Minimap`, `MouseCursor`, `ScreenCapture`), DemoGame and the SceneEditor files see it.
  Marbles uses none.

### Baseline: D3D11, before any change

Survival at `Solution\x64\Release\Survival.exe` (built 2026-10-15 01:18), `--windowed`, the user's
`%LOCALAPPDATA%\Survival\settings.json` (GI off, RT off, ray traced shadows on, solver `both`,
motion blur on, 1920x1080), `ui.render_fps` lifted to 1000 for the run (game.json restored after),
New Game in the default 100-chunk world, fog render off and everything revealed, 10 s sampled after 6 s
of settling, from `perf` deltas. Script: a scratch `Measure-Baseline.ps1` (not in the repo); rerun the
same way after each milestone that renders.

| view | fps | frame |
|---|---|---|
| default start view | 75.7 | 13.2 ms |
| middle of the busiest forest | 103.4 | 9.7 ms |

CPU laps of `RenderSystem::Draw`, ms per frame:

| lap | default | forest |
|---|---|---|
| scene | 0.660 | 0.574 |
| shadows | 0.401 | 0.407 |
| depth | 0.300 | 0.246 |
| lights | 0.105 | 0.072 |
| latch | 0.045 | 0.039 |
| lods | 0.040 | 0.034 |
| visibility | 0.040 | 0.039 |
| post | 0.036 | 0.034 |
| signature | 0.010 | 0.008 |
| static_shadows | 0.000 | 0.000 |
| **total** | **1.64** | **1.45** |

GPU stamps, ms per frame (some nest: `post` sums its passes, `deferred` is lighting inside the scene):

| stamp | default | forest |
|---|---|---|
| scene | 8.180 | 4.849 |
| deferred | 5.082 | 1.599 |
| post | 1.899 | 1.944 |
| shadows | 1.536 | 1.410 |
| mix | 0.755 | 0.764 |
| depth | 0.422 | 0.369 |
| light | 0.418 | 0.417 |
| aa | 0.322 | 0.326 |
| motion | 0.286 | 0.322 |
| tile_cull | 0.133 | 0.136 |
| blur | 0.118 | 0.115 |

**Reading of the baseline.** The render CPU laps total 1.4-1.6 ms inside a 9.7-13.2 ms frame, and the
non-nested GPU stamps (scene + post + shadows + depth, ~12 ms in the default view) account for nearly
the whole frame: in these scenes the frame is GPU-bound and `RenderSystem::Draw` is already cheap on
the CPU (instancing, culling, static shadow map). The laps do not include the sim tick, physics, the
ray tracing thread, or driver/present waits, so this is not the whole CPU picture. It does bound what
M7 can win from draw-call overhead alone in these scenes: at most the ~1.5 ms of the render laps.
