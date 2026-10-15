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
| M7 | GPU frame-time work (re-scoped 2026-10-15, see below), measured against the baseline | **not started** |

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
| `HOTBITE_LOG=<file>` | engine log written to that file from `DXCore::InitDirectX` (device, adapter, frame cost every 240 frames, PSO compiles >= 5 ms) |
| `HOTBITE_SHADER_DEBUG=0` | the shaders the editor compiles at run time (reload, new shader) leave the debug symbols out |
| `HOTBITE_GFX_TRACE=<serial>` | in the log, every draw and dispatch of 3 frames from that frame serial, with what each had bound |
| `HOTBITE_GFX_TRACE_CREATE=1` | traces resource / view creation |
| `HOTBITE_DUMP_DIR=<dir>` (+ `HOTBITE_DUMP_FRAME`) | once, at frame 200: the frame's main targets, the post chain and the back buffer as PNGs in that folder |

The renderer logs `gfx cpu per frame` every 240 frames: the CPU time waiting on the GPU, and the immediate
uploads (an immediate command list per upload, a ring of 16 in `GfxDevice`) that block.

## Performance (user's settings, `Measure-Baseline.ps1`, fps cap lifted)

**Known problem, left open for later: DXC compiles the ray traced lighting shader 2-2.5x slower than fxc.** The same
HLSL of `DeferredLightRaysCS` costs 22.0 ms built by DXC (`cs_6_0`) and 8.7 ms built by fxc (`cs_5_0 /O3`) in the
stress scene below; the rest of the frame is not affected. **For now `DeferredLightCS` and `DeferredLightRaysCS` are
built by fxc**: `HotBiteFxc` metadata on their `FxCompile` items in `Engine.vcxproj` (read by `Dxc.targets`, which
runs `fxc.exe /O3` on them), and `UsesFxc` in `ShaderCompiler.cpp` for the editor's hot reload (a reload of the rays
shader takes ~90 s at /O3, on a worker thread). They are compute shaders, so each is a pipeline of its own and mixing
SM5 and SM6 is fine (a single pipeline cannot mix them). To go back to DXC once it is fixed: remove the metadata and
the list. Where to start: PIX on the DXC build of suite `67-rt-shadow-stress` (the symbols are in the `.cso`), compare
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

**Next (M7), by expected payoff:** (1) find why DXC is slow on the lighting shader and put it back on DXC (see the
known problem above), and compare the other big shaders (`GIRayTraceCS`, `RayTraceCS`, `DenoiserCS`) DXBC against
DXIL the same way - the lighting pair on fxc is the stopgap, restructuring the ray code (tile classification,
packed ray lists as `feedback_pack_rays_for_warps`) the other road; (2) explicit barriers / UAV
overlap instead of the full serialisation between the compute passes; (3) async compute for the
independent ones; (4) fewer full-screen post passes.

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
