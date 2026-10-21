# Dynamic resolution (DRS) with NVIDIA DLSS

The scene is drawn at an internal size that a controller changes while the game runs, so the GPU finishes every frame inside
the budget of a target refresh rate (60 Hz by default); NVIDIA DLSS Super Resolution reconstructs the native picture from it.
It is a **per-level option** like the post-process, GI and ray tracing ones, and it **exists only where DLSS does**: no RTX card,
an old driver, no `nvngx_dlss.dll`, or an engine built without the SDK, and the option is unavailable (the level renders at the
native size and keeps the request in its file).

## Getting the SDK

DLSS is NVIDIA's SDK, and its licence (`ThirdParty/DLSS/LICENSE.txt`, "NVIDIA RTX SDKs LICENSE") does not let us copy or
redistribute it on its own. So it is not in this repository: it is a **git submodule** that points at NVIDIA's own public repo.

```
git clone --recurse-submodules <HotBiteEngine url>          # a new clone
git submodule update --init --depth 1 ThirdParty/DLSS       # a clone that already exists
```

It is about 1.6 GB (the repo carries the libraries of every platform; only `include/` and `lib/Windows_x86_64/` are used).
Using it means accepting that licence. A game that ships dynamic resolution has to ship `nvngx_dlss.dll` (Survival's project copies it
next to its exe) and follow the licence's distribution requirements. Without the submodule everything builds and `Core::Dlss`
is a stub: `RenderSystem::DynamicResolutionAvailable()` is false.

`Engine.vcxproj` finds it by itself (`DlssDir`, `HasDlss`): it adds the include path, the `HOTBITE_DLSS` define and merges
`nvsdk_ngx_d.lib` into `Engine.lib`. At run time NGX looks for the dll next to the exe, in `HOTBITE_DLSS_DIR`, and in the
submodule's `lib/Windows_x86_64/rel` of the checkout the engine was built from.

## The option

Per level, in the level's `editor.render` block (the Scene Editor's Render menu writes it, and `RenderSettings::ApplyFromJson` reads it):

| key | meaning |
|---|---|
| `drs` | on / off (default off) |
| `drs_hz` | the refresh rate to hold, 24..360 (default 60) |
| `drs_min_scale`, `drs_max_scale` | the smallest and biggest share of the native size the controller may use (default 0.5 and 1; DLSS takes 0.5 to 1 in the mode used) |

Automation (`render` command of the editor): the same keys, `drs_fixed_scale <0..1>` pins the scale (0: the controller decides; never saved),
and the dump adds `drs_available`, `drs_message` (why not, or the last thing DLSS said), `drs_active`, `drs_scale`, `drs_width/height`,
`drs_native_width/height`, `drs_gpu_ms`, `drs_budget_ms`, `drs_changes`, `drs_upscaled_frames`.

In code: `RenderSystem::SetDynamicResolution(bool)` (returns whether it is on: false where DLSS is not there),
`SetDynamicResolutionTargetHz`, `SetDynamicResolutionRange`, `SetDynamicResolutionFixedScale`, `GetDynamicResolutionStats`.
A game with its own scene options reads the same keys from its own data (Survival: the `render` block of each world type in
`Assets/Data/World/world_types.json`, applied by `Presentation::ApplySceneRender`).

## The level's frame rate follows the target

`World::Run` registers the render timer with `1 / render_fps` and the scheduler sleeps until the next period, so a level that runs
at 60 can never present more than 60 frames however small the picture is drawn, and one that runs faster than the target renders frames
the picture could have used. While dynamic resolution is on, `RenderSystem::RequestedRenderFps()` is the target and the
render timer changes its period to it (`Scheduler::SetTimerPeriodAsync`: a callback runs with the scheduler locked, so the change is queued and applied at
the start of the next `Update`); off, it goes back to the rate `Run` was given. The background (simulation) and physics threads keep their own rates.

Measured on an RTX 4050 laptop (Survival, small world, 1540x845): the level at 60 holds 60; a target of 90 runs at exactly 90 (it would stay
at 60 without this); with the level at 140 and no DRS the game runs at ~92 fps (10 ms of GPU), with DRS at a 140 target ~128 fps
(scale at its 0.5 floor, 6 ms), the floor being DLSS's.

## How it works

1. **Every screen-sized target exists in 12 sizes** (`Core::RenderScale`, steps of 0.92 down from native), made with
   `RenderTexture2D::InitScaled` / `DepthTexture2D::InitScaled`. The sizes are *placed resources over one heap per texture*
   (`GfxDevice::CreateAliasedTextures2D`), so all of them cost the memory of the biggest and switching is an aliasing barrier
   (`GfxContext::SwitchAliased`): nothing is allocated while the game runs and no frame stalls. A switch clears every target
   (the memory held another size), so the temporal passes start over: the TAA history, the GI/RT accumulation (`ResetRTBBuffers`),
   DLSS (`InReset`). Every shader reads the size of the texture it is given (`GetDimensions`), so none of them know about the scale. With the option off nothing of
   this is built: the targets are made as before, until the first level that turns it on converts them.
2. **The scene is drawn at the internal size** (G-buffer, lighting, ray tracing, GI, volumetrics, tiled lights, the jittered projection).
   `RenderSystem::RenderWidth/Height()` is that size and `render_viewport` its viewport.
3. **After the lighting is mixed (`ProcessMix`) DLSS makes the native picture** (`ProcessDlss`), instead of the TAA (it is the temporal filter). Inputs: the mixed
   colour, the hardware depth (`depth_view`), the motion vectors in render pixels (written by `MotionCS` beside the engine's own, so no extra pass:
   the other seven consumers read the old format and the `-FLT_MAX` "no history" marker), the Halton jitter of `8 * (native / render)^2` phases.
4. **Everything that is not the scene is native**: motion blur (`MotionBlurCS` reads the native picture and a motion texture of another size),
   depth of field, the lens, the game's own stages and the interface. `PostProcess::SetInputOverride` makes the first stage read DLSS's picture
   instead of the texture the scene was drawn into.
5. **The controller** (`Core::DynamicResolutionController`, no graphics calls) is fed the GPU time of finished frames (two timestamps around
   the frame, read some frames later together with the step each frame was drawn at). Down is fast (one jump to the step a cost model says fits,
   with the part of the frame that scales with the pixels measured after each change), up is slow with hysteresis (headroom for about a second,
   not right after coming down). The budget is 0.9 of the frame time.

## Limits

* Windows / D3D12 / NVIDIA RTX only. Screen-sized targets only: the shadow maps and the ray tracing structures do not scale.
* The sky, which has no motion vectors (no previous position), is reconstructed without them: a camera that looks at it and turns ghosts it.
* DLSS sharpening is not used (deprecated by NVIDIA); `rt_quality` and the other divider-based settings keep their meaning at whatever size is drawn.
* The depth of field's kernel is sized in native pixels.
* If DLSS fails once (`NGX error` in `drs_message`) the option turns itself off and the next frame is native.

## Tests

`Tools/SceneEditor/automation/tests/suites/53-dynamicresolution.tests.ps1` (the option, its validation, the level round trip, the upscale itself, skipped
where there is no DLSS) and Survival's `76-dynamic-resolution.tests.ps1` (the data, the switch, the size, the render loop following the target, the controller).
