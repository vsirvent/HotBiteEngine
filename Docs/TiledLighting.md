# Tiled deferred lighting

Every point and spot light of a frame is packed into **one 1024 entry buffer**; a compute
pass records, for each **16x16 screen tile**, a **1024 bit mask** of the lights whose range
reaches it; and the lighting is added to the G-buffer **afterwards**, by a second compute
pass that visits only the lights in its tile's mask. The geometry passes evaluate no light.

It replaced a forward design in which every material pixel shader carried the scene's lights
in a constant buffer (`MAX_LIGHTS` = 8 point lights, uploaded again for every draw call) and
looped over all of them for every pixel it shaded.

## The frame

```
GatherLights        CPU: the nearest 1024 point lights -> packed buffer; 8 shadow slots; aux lists
CastShadows         dynamic + point shadow maps (only the lights that hold a slot draw a cube map)
DrawDepth, DrawSky
DrawScene (pass 1)  G-buffer: albedo, emission, normal, position, material map. NO light.
ProcessDeferredLighting(0)   TileLightCullCS -> DeferredLightCS : light of everything drawn so far
DrawScene (pass 2)  only when the level has second-pass entities (water): its own G-buffer
ProcessDeferredLighting(1)   lights only what pass 2 drew (material map w & GBUFFER_PASS2)
DrawSplats          Gaussian splats write G-buffer + material (SPLAT model)
ProcessDeferredLighting(2)   lights only the splats, weighted by their coverage
ProcessMotion / RT / GI / volumetric fog / ProcessMix ...  as before
```

Each `ProcessDeferredLighting` culls again, against the G-buffer *as it stands*: the surfaces
a later pass has overwritten are the ones to light, and a stale mask would miss a light that
only reaches the new surface.

The water case is why the first dispatch runs *before* the second pass: WaterPS reads the
**lit** colour of what lies under it (`prevLightTexture`).

## Data

**`PackedLight`** - 32 bytes, `Core/TiledLights.h` / `Common/TiledLights.hlsli`:

| field | |
|---|---|
| `float3 position`, `float range` | exact |
| `uint color` | RGB9E5: 9 bit mantissas under one shared exponent (~0.3 % per channel at any intensity) |
| `uint direction` | spot axis, octahedral, two int16 |
| `uint cones` | cos(outer), cos(inner) as two float16 |
| `uint flags` | bit 0 spot, bit 1 has a shadow map, bits 2.. the slot in `PointShadowMapTexture` |

**Tile mask** - `TILE_MASK_WORDS` = 32 `uint`s per tile in a raw `ByteAddressBuffer`
(`tile * 32 + word`); bit `i` of the tile is light `i` of the buffer. At 1080p that is 8160
tiles x 128 bytes = 1 MB.

**Material map** - the G-buffer's eighth target (`RGBA16F`), the one thing the lighting needs
that the other seven do not carry. `w` is the shading model (+ `GBUFFER_PASS2`); what `x y z`
mean depends on it - see `Common/GBuffer.hlsli` for the table.

| model | what it is |
|---|---|
| `STANDARD` | spec intensity, bloom scale, the parallax self shadow of the *first directional light* |
| `WATER` | `depth/clarity` (transmission is `exp(-x * WATER_EXTINCTION)`), the shading normal's x and z |
| `LAVA` | the weight (`0.6 * shore fade`) of the sun and point lights it takes |
| `SPLAT` | spec, bloom scale, the cloud's alpha, which weights **all** of its light |

## Culling (`TileLightCullCS`)

One group per tile, one thread per pixel. The group reduces the G-buffer positions of its
pixels to the nearest and farthest **view depth of any surface** in the tile, then each of its
256 threads tests four lights: the light's range sphere against the tile's four side planes
(through the eye) and against that depth slab. Survivors `InterlockedOr` their bit into group
shared memory, which the group writes out. The test is conservative (a sphere that touches the
region is kept) so the shading pass trusts the mask completely.

The depth slab is what makes it better than a screen-space test: a light behind the wall a
tile is looking at, or floating in front of it out of range, is dropped however well it lines
up on screen. A tile with no surface (sky) has an empty mask.

## Shading (`DeferredLightCS`)

One group per tile - the group *is* the tile, so the mask address is uniform across it - one
thread per pixel. Ambient + the directional lights (cascaded shadows, cloud shadow, as before)
+ every set bit of the tile's mask. The lighting functions are `Common/DeferredLighting.hlsli`,
the shadow lookups `Common/LightShadows.hlsli`. The result is **added** to the light and bloom
maps, on top of the emission the G-buffer pass left there, so the mixer, the debug buffer
views and everything downstream see the same two buffers they always did.

Shading models: the standard one is the old `CalcDirectional`/`CalcPoint` ported term for term
(Lambert + back reflex + Blinn specular, range attenuation, spot cone, PCF shadows); water and
lava are the old `CalcWater*`/`CalcLava*` ported into the same file.

## What is *not* in a tile, and why

A pixel shader that shades a **surface on screen** reads the masks. Three things cannot, and
read **the nearest 8 point lights** from a small constant buffer instead (`AuxLight`, 48
bytes, `MAX_AUX_LIGHTS`):

* **Ray hits** (`RayTraceCS`, `GIRayTraceCS`, `RenderDustCS`): a hit can be anywhere in the
  world, behind the camera or off screen; it has no tile. `SimpleLight.hlsli` holds the one
  place they evaluate a light (`HitDirectional`/`HitPoint`: Lambert, one shadow tap).
* **Glows and flares** (`SkyPS`, `LavaPS`, `LensFlareCS`): the visible halo of a lamp is an
  effect on the picture of the lamp, not lighting, and a thousand of them would not be wanted.
* **The volumetric march** (`VolumetricLightCS`): per pixel, along a ray; it takes the nearest
  lights *that have a fog density*.

With 8 or fewer point lights these see exactly the lights they always did.

## Shadows

A shadow map is a cube texture in a shader resource slot, and six passes over the casters, so
shadows stay limited to **`MAX_LIGHTS` (8) lights at once**: `GatherLights` gives the slots to
the nearest shadow-casting lights, and only those render a cube map. The rest light, unshadowed.
(The light count is not limited by this; the shadow count is.)

The slot is *data* (it comes out of the packed light), and fxc cannot index a texture array by
data - it tries to unroll the loop that produced the index and fails. `GatherPointShadow` /
`SamplePointShadow` match the slot against each map by a literal index, one branch per slot,
one taken.

## Ray traced shadows

A point light with `raytraced_shadow` (or a directional light with `raytraced_shadow`) holds no
shadow map: no cube map or cascades in memory, no six passes over the casters. `ShadePoint` /
`ShadeDirectional` trace **one any-hit ray** from the surface to the light instead
(`Common/ShadowRays.hlsli`). It is exclusive with `cast_shadow`: turning either on turns the other
off. A point light carries it as `PACKED_LIGHT_RAYTRACED` (bit 6 of the packed flags), a sun as
`DIR_LIGHT_FLAG_RAYTRACED`.

**The scene the rays go through** (`Core::ShadowRays`, built by `RenderSystem::BuildShadowRays` each
frame after `GatherLights`): the shadow casters whose boxes a ray traced light's range reaches (for a
sun: whatever lies within its cascade shadow distance + `SHADOW_RAY_SUN_LENGTH` of the camera), at
most 2048, nearest first; a top level BVH over their world boxes, a structured buffer of their
transforms, and the mesh BVHs / vertex / index buffers the reflection tracers already use (LOD 1 of
a mesh with a chain). Nothing is built when no light traces.

**Soft edges.** Each ray aims at a different point of a ball round the light (`shadow_ray_radius`,
a cone of tangent `shadow_ray_sun_spread` for the sun) - per pixel, and per frame along an R3
low discrepancy sequence. That is noise, so the light of the ray traced lights goes to its own
buffer and `ShadowAccumulateCS` averages it over the frames (reprojected through `prev_position_map`,
depth validated, history clamped to the 3x3 neighbourhood so a moved caster snaps instead of fading)
before adding it to the light map. Only the first lighting dispatch accumulates; water and splats add
their ray traced light directly. Tune with the `shadow_rays [radius blend]` editor command.

**Not done:** skinned meshes do not cast (their BVH is the bind pose); water and lava ignore it;
volumetric fog and the reflection / GI tracers do not see a ray traced sun's shadow.
Tests: editor suites `46-raytracedshadows` (point) and `47-raytracedsun`.

## Limits and things worth knowing

* **1024 point lights** per frame; past that the nearest 1024 to the camera are lit.
* The G-buffer is **8 render targets**, the D3D11 maximum; adding a ninth means repacking.
* `SplatRasterCS` writes ten UAVs, which needs feature level **11_1**; below it splat clouds
  are not drawn (`DrawSplats` returns).
* The deferred pass reads and writes `bloom_map` (R11G11B10) through a typed UAV, as
  `SplatRasterCS` already did.
* **Parallax self shadow** is kept for the first directional light only (a march through the
  height map cannot be redone after the fact); point lights no longer self-shadow a parallax
  surface.
* **Lava** is lit through the light map; it used to add the light to its own colour. Slightly
  different look; DemoGame is the only user.
* The light of a **parallax-displaced** pixel is evaluated at the undisplaced position (the
  G-buffer's).
* A Gaussian splat's **partial edge** pixel (coverage under `surface_alpha`) does not own the
  G-buffer, so it keeps the light of what is behind it.
* `MaterialPreviewPS` (the editor's thumbnail studio) has its own three fixed lights and is not
  part of the scene render.
* Particle shaders (`DrawParticles` is not called by anything) take the ambient colour and the
  directional colour, with no shadow lookup.

## Looking at it

* Automation (Scene Editor and Survival): `lights_info` (lights packed, shadow slots, tiles lit,
  most lights in a tile, average over lit tiles), `lights_validate` (brute-force check of every
  pixel against every light: lights that reach a pixel and are missing from its mask - 0),
  `tile_culling on|off` (off = every light in every tile: the reference), `spawn_lights`,
  `clear_lights`, `gpu_timings` / `perf` (`tile_cull`, `deferred`).
* API: `RenderSystem::GetTiledLightStats`, `ValidateTileLights`, `SetTileCulling`,
  `GetGpuTimings().tile_cull/.deferred`, `World::CreatePointLight`.

## Measured

Scene Editor, 60x60 floor, camera above it (suite `44-tiledlights`): 1024 lights over the
floor - a lit tile holds ~12 of them on average (the busiest 19); `TileLightCullCS` 0.07 ms,
`DeferredLightCS` 0.42 ms; the same frame with the culling off (every light at every pixel):
11.4 ms, 27 times the shading. 100 lights: 0.07 ms + 0.43 ms. (Most of the 0.4 ms is what the
pass costs with *no* point light: ambient + the sun's cascaded shadows for every pixel.)

Survival's terrain at night, 1540x845 (suite `57-tiled-lights` there): 1024 lights over
150 x 150 units - 2,366 of 5,141 tiles lit, a lit tile holds 13.3 of them on average, the
busiest 28; culling 0.13 ms + shading 0.74 ms against 17.6 ms with the culling off; the game
at its 60 fps cap with or without them, 39 fps with the culling off. Choosing and packing the
lights on the CPU: 0.025 ms.
