/*
The HotBite Game Engine

Copyright(c) 2023 Vicente Sirvent Orts

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

//Shades the G-buffer with light, one permutation of this file for each kind of surface and light
//(RenderSystem::ProcessDeferredLighting dispatches them one after the other over the same tiles,
//each adding to the light map and the bloom map what its own pixels and lights are owed):
//
//  (nothing defined)         the surfaces that are neither water nor lava: ambient, every directional light
//                            and every point light whose shadow is a shadow map or none (no ray code in it).
//  DEFERRED_LIGHT_RAYS       the same surfaces, only the lights that trace their shadow (flagged ray traced),
//                            with the ray tracer compiled in and no shadow map code. Dispatched when the frame has
//                            such lights; with nothing to trace against it lights them unshadowed.
//  DEFERRED_LIGHT_WATER      the water: its own ambient, directional and point light, no shadows.
//  DEFERRED_LIGHT_LAVA       the lava: directional and point light with shadow maps, no rays.
//
//DeferredLightRaysCS.hlsl, DeferredLightWaterCS.hlsl and DeferredLightLavaCS.hlsl are this file with the
//define; the *FxcCS.hlsl twins of the four are the same again, only built by fxc instead of DXC. It is one HLSL
//for both compilers: nothing in it is conditional on the compiler (what fxc needs - no `continue` in the lights'
//loops, see THIS_PERMUTATION_LIGHTS - DXC takes as well). They are separate shaders because one shader holding all
//of it keeps the registers of its most demanding branch - the BVH walk and the screen march of the shadow rays -
//for every pixel, whatever it is: the GPU then holds fewer pixels in flight (occupancy) and the whole pass slows
//down.
//
//One thread group per 16x16 screen tile - the same tile TileLightCullCS filled a mask
//for - and one thread per pixel. The group reads its tile's mask once (the address is the
//same for every thread, so the loads are uniform) and every pixel walks the set bits:
//the handful of the frame's 1024 point lights whose range reaches this tile's surfaces.
//Directional lights, which reach everything, are looped over directly.
//
//What it adds goes into the light map and the bloom map, on top of what the G-buffer pass
//left there (the surface's own emission), so the mixer sees the same two buffers it
//always did.

//TEMP: ray counters (Common/ShadowRays.hlsli), read back by RenderSystem::GetShadowRayCounters.
//#define SR_STATS 1

#include "../Common/Defines.hlsli"
#include "../Common/ShaderStructs.hlsli"
#include "../Common/PixelCommon.hlsli"

cbuffer externalData : register(b0)
{
	AmbientLight ambientLight;
	DirLight dirLights[MAX_LIGHTS];
	int dirLightsCount;
	float3 cameraPosition;
	int screenW;
	int screenH;
	int tiles_x;
	//Which surfaces this dispatch lights. 0 everything, 1 only what the second pass drew
	//(GBUFFER_PASS2), 2 only the Gaussian splats. The passes that draw over the first one
	//run after the first dispatch has lit what was there, and must not light it twice.
	int pass_filter;
	float4 LightPerspectiveValues[MAX_LIGHTS / 2];
	matrix DirPerspectiveMatrix[DIR_SHADOW_MATRIX_COUNT];
	matrix DirStaticPerspectiveMatrix[MAX_LIGHTS];
	float time;
	float cloud_density;
	//Objects in the ray traced shadow scene (Core::ShadowRays); 0 when no light needs it.
	uint shadow_ray_objects;
	//The scene a ray traced directional light's rays go through (SR_NO_SCENE: none). The point lights name
	//theirs in their flags.
	uint shadow_ray_sun_scene;
	//Radius of the ball the shadow rays aim into round each light, and the frame counter that
	//moves the aim from one frame to the next (soft edges once the frames are averaged).
	float shadow_ray_radius;
	uint shadow_ray_frame;
	//1: the light of the ray traced lights goes to rt_shadow_out, not the light map, and
	//ShadowAccumulateCS averages it over the frames and adds it (their jittered edges are noise
	//until then). 0: it is added to the light map like any other light's.
	int shadow_ray_accumulate;
	//Tangent of the cone a ray traced directional light's rays are aimed into, and how far from
	//the camera such a light is traced at all (the shadow distance of its cascades).
	float shadow_ray_sun_spread;
	float shadow_ray_sun_distance;
	//The screen space half of the ray traced shadows (RayScreenSolver.hlsli): the camera's
	//view-projection, the pyramid's ratio, how many steps one ray may take (0 turns the screen
	//off), how far it looks, what counts as behind a surface (a bias, and a thickness that
	//grows with the surface's distance) - see ScreenShadowOccluded.
	matrix ss_view_proj;
	float ss_hiz_ratio;
	int ss_steps;
	float ss_max_distance;
	float ss_bias;
	float ss_thickness;
	float ss_thickness_rel;
	//Most point lights that trace their shadow that are traced for a pixel in a frame (DEFERRED_LIGHT_RAYS), 0 for all of them. The
	//others are left for the other frames, and the light of those that are traced is scaled up to make up for them: see main.
	int shadow_ray_max_lights;
}

Texture2D<float4> positionTexture : register(t0);
//rt_ray_sources1: the world normal in xyz.
Texture2D<float4> normalTexture : register(t1);
Texture2D<float4> materialTexture : register(t2);
StructuredBuffer<PackedLight> lightsBuffer : register(t3);
ByteAddressBuffer tileMasks : register(t4);
//The Hi-Z pyramid of the depth map (level 0 is depth_map), after the ray traced shadow scene's
//t5-t9 (Common/ShadowRays.hlsli). Only the permutation that traces rays (DEFERRED_LIGHT_RAYS) has any of it.
#ifdef DEFERRED_LIGHT_RAYS
#include "../RayTrace/RayScreenSolver.hlsli"
Texture2D<float> ssHiZ[HIZ_TEXTURES] : register(t10);
#endif

RWTexture2D<float4> light_out : register(u0);
RWTexture2D<float4> bloom_out : register(u1);
RWTexture2D<float4> rt_shadow_out : register(u2);
#ifdef SR_STATS
RWByteAddressBuffer sr_counters : register(u3);
#endif

#ifdef DEFERRED_LIGHT_RAYS
//This permutation lights only the lights that trace their shadow: Common/DeferredLighting.hlsli leaves out of it what only a light with
//a shadow map uses (the cascades, the PCF, the cube maps).
#define DL_RT_LIGHTS_ONLY 1
//Whether the screen space march is on this frame.
bool ScreenShadowActive()
{
	return ss_steps > 0;
}

//Whether the depth buffer shows a caster between `from` and the light, `len` away along `dir`.
//What the BVH walk cannot see - a skinned mesh - is in the depth buffer like everything else
//that is drawn, and a ray that goes behind a surface by about as much as a body is thick has
//gone through it. The normals play no part: a caster faces any way, and the facing test would
//take away exactly the ones lit from behind.
bool ScreenShadowOccluded(float3 from, float3 dir, float len)
{
	uint w, h;
	positionTexture.GetDimensions(w, h);
	uint2 hit;
	//The bias and the thickness both grow with the distance from the camera, the bias to keep a
	//surface from shadowing itself at a pixel's reach, the thickness to follow the steps.
	const float dist = length(from - cameraPosition);
	return ScreenTrace(from, dir, cameraPosition, float2(w, h), ss_view_proj, ssHiZ, ss_hiz_ratio,
		min(len, ss_max_distance), ss_steps, ss_bias + 0.004f * dist, ss_thickness, ss_thickness_rel, false,
		positionTexture, normalTexture, hit);
}

#define SR_SCREEN_SHADOWS 1
#include "../Common/ShadowRays.hlsli"
#endif

#include "../Common/DeferredLighting.hlsli"

//Whether a directional light traces its shadow (it has no shadow map of its own).
bool DirLightIsRayTraced(DirLight light)
{
	return !light.cast_shadow && (light.flags & DIR_LIGHT_FLAG_RAYTRACED) != 0;
}

//Whether a light is this permutation's, given whether it traces its shadow (its own flags say so, in the buffer of the point lights
//and in the directional light's data: nothing is passed in to tell a permutation which lights to take). The one with the ray tracer
//takes the lights that trace, the first one the rest; each walks the tile's bits and leaves the other kind alone, so a light is
//lit by exactly one of them. (A condition rather than a `continue`: see the note in main.)
#ifdef DEFERRED_LIGHT_RAYS
#define THIS_PERMUTATION_LIGHTS(rt) (rt)
#else
#define THIS_PERMUTATION_LIGHTS(rt) (!(rt))
#endif

//What a permutation owes a pixel goes on top of what the light map and the bloom map hold.
void AddToMaps(uint2 px, float3 lum, float3 bloom, float weight, uint model)
{
	if (any(lum != 0.0f)) {
		const float4 l = light_out[px];
		light_out[px] = float4(l.rgb + lum * weight, l.a);
	}
	if (any(bloom != 0.0f)) {
		const float4 bl = bloom_out[px];
		//A cloud's own bloom was never clamped; everything else always was.
		const float3 nb = (model == SHADING_SPLAT) ? (bl.rgb + bloom * weight)
		                                           : saturate(bl.rgb + bloom);
		bloom_out[px] = float4(nb, 1.0f);
	}
}

[numthreads(TILE_SIZE, TILE_SIZE, 1)]
void main(uint3 gid : SV_GroupID, uint3 dtid : SV_DispatchThreadID)
{
	if (dtid.x >= (uint)screenW || dtid.y >= (uint)screenH) {
		return;
	}
	const uint2 px = dtid.xy;
#ifdef DEFERRED_LIGHT_RAYS
	sr_pixel = px;
#endif
	//The material first: a pixel of another kind of surface is turned away before anything else is read.
	const float4 mat = materialTexture[px];
	const uint code = (uint)(mat.w + 0.5f);
	const uint model = code & GBUFFER_MODEL_MASK;
	if (pass_filter == 1 && (code & GBUFFER_PASS2) == 0) {
		return;
	}
	if (pass_filter == 2 && model != SHADING_SPLAT) {
		return;
	}
#if defined(DEFERRED_LIGHT_WATER)
	if (model != SHADING_WATER) {
		return;
	}
#elif defined(DEFERRED_LIGHT_LAVA)
	if (model != SHADING_LAVA) {
		return;
	}
#else
	if (model == SHADING_WATER || model == SHADING_LAVA) {
		return;
	}
#endif
	const float4 wp = positionTexture[px];
	//Nothing was drawn here: the G-buffer is cleared to FLT_MAX.
	if (wp.x >= 1.0e30f) {
		return;
	}

	const float3 P = wp.xyz;
	const uint tile = gid.y * (uint)tiles_x + gid.x;
	const uint mask_base = tile * TILE_MASK_WORDS * 4;
	float3 lum = float3(0.0f, 0.0f, 0.0f);
	float3 bloom = float3(0.0f, 0.0f, 0.0f);
	float weight = 1.0f;
	int i;

#if defined(DEFERRED_LIGHT_WATER)
	//The ray source normal is deliberately flattened (it is what reflections follow);
	//the shading normal, wave bumps and all, travels in the material map.
	const float ny = sqrt(max(0.0f, 1.0f - mat.y * mat.y - mat.z * mat.z));
	const float3 N = float3(mat.y, ny, mat.z);
	lum = ShadeAmbient(N);
	for (i = 0; i < dirLightsCount; ++i) {
		lum += ShadeWaterDirectional(N, P, dirLights[i], bloom);
	}
	for (uint w = 0; w < TILE_MASK_WORDS; ++w) {
		uint bits = tileMasks.Load(mask_base + w * 4);
		while (bits != 0u) {
			const uint b = firstbitlow(bits);
			bits &= bits - 1u;
			const PackedLight L = lightsBuffer[w * 32u + b];
			if (length(P - L.position) < L.range) {
				lum += ShadeWaterPoint(N, P, L, bloom);
			}
		}
	}
	//Only the share of the column the surface itself scatters back; WaterPS already
	//put the light of the bottom, weighted by what is transmitted, in the light map.
	lum *= (1.0f - exp(-mat.x * WATER_EXTINCTION));
#elif defined(DEFERRED_LIGHT_LAVA)
	const float3 N = float3(0.0f, 1.0f, 0.0f);
	for (i = 0; i < dirLightsCount; ++i) {
		lum += ShadeLavaDirectional(N, float4(P, 1.0f), dirLights[i], i);
	}
	for (uint w = 0; w < TILE_MASK_WORDS; ++w) {
		uint bits = tileMasks.Load(mask_base + w * 4);
		while (bits != 0u) {
			const uint b = firstbitlow(bits);
			bits &= bits - 1u;
			lum += ShadeLavaPoint(N, P, lightsBuffer[w * 32u + b]);
		}
	}
	weight = mat.x;
#else
	//STANDARD and SPLAT share the model; they differ in what `z` weights.
	const float3 N = normalTexture[px].xyz;
	const float spec = mat.x;
	const float bloom_scale = mat.y;
	const float sun = (model == SHADING_STANDARD) ? mat.z : 1.0f;
	if (model == SHADING_SPLAT) {
		weight = mat.z;
	}
#ifdef DEFERRED_LIGHT_RAYS
	//The light of the lights that trace goes to its own map while ShadowAccumulateCS averages it over the frames (shadow_ray_accumulate).
	float3 lum_rt = float3(0.0f, 0.0f, 0.0f);
#else
	lum = ShadeAmbient(N);
#endif
	//(No `continue` in these loops: fxc has to unroll the directional one - the shadow maps are indexed by the loop's counter - and
	//cannot unroll a loop that has one.)
	for (i = 0; i < dirLightsCount; ++i) {
		if (THIS_PERMUTATION_LIGHTS(DirLightIsRayTraced(dirLights[i]))) {
			const float3 dir_light = ShadeDirectional(N, float4(P, 1.0f), spec, bloom_scale, dirLights[i],
			                        cloud_density, i, bloom) * ((i == 0) ? sun : 1.0f);
#ifdef DEFERRED_LIGHT_RAYS
			if (shadow_ray_accumulate != 0) {
				lum_rt += dir_light;
			}
			else {
				lum += dir_light;
			}
#else
			lum += dir_light;
#endif
		}
	}
#ifdef DEFERRED_LIGHT_RAYS
	//A pixel traces at most shadow_ray_max_lights of the lights that reach it in a frame. When more reach it, a window of that many of
	//them is traced, its start different for every pixel and moving on by the window's size with every frame (shadow_ray_frame, as the
	//ray jitter does), so that the frames take turns at all of them; the light of the traced ones is scaled by reach / max, the share
	//of the lights each frame leaves out, so that what the frames add up to is the same as with all of them. ShadowAccumulateCS
	//averages the frames, which is why this is done only for the light that goes to it (shadow_ray_accumulate): the passes that add
	//their light straight to the map have no frames to average and trace every light.
	uint reach = 0u;
	if (shadow_ray_accumulate != 0 && shadow_ray_max_lights > 0) {
		for (uint cw = 0; cw < TILE_MASK_WORDS; ++cw) {
			uint cbits = tileMasks.Load(mask_base + cw * 4);
			while (cbits != 0u) {
				const uint cb = firstbitlow(cbits);
				cbits &= cbits - 1u;
				const PackedLight CL = lightsBuffer[cw * 32u + cb];
				if (LightIsRayTraced(CL) && PointLightReaches(CL, P)) {
					++reach;
				}
			}
		}
	}
	const uint max_lights = (uint)max(shadow_ray_max_lights, 0);
	const bool sampled = reach > max_lights;
	const uint first = sampled ? (SrHash(px.x + SrHash(px.y)) + (uint)shadow_ray_frame * max_lights) % max(reach, 1u) : 0u;
	const float light_scale = sampled ? (float)reach / (float)max(max_lights, 1u) : 1.0f;
	uint ordinal = 0u;
#endif
	for (uint w = 0; w < TILE_MASK_WORDS; ++w) {
		uint bits = tileMasks.Load(mask_base + w * 4);
		while (bits != 0u) {
			const uint b = firstbitlow(bits);
			bits &= bits - 1u;
			const PackedLight L = lightsBuffer[w * 32u + b];
			if (THIS_PERMUTATION_LIGHTS(LightIsRayTraced(L))) {
#ifdef DEFERRED_LIGHT_RAYS
				bool traced = true;
				if (sampled) {
					//The lights that reach the pixel, in the order of the tile's bits; the ones that do not are not in the window.
					traced = false;
					if (PointLightReaches(L, P)) {
						traced = ((ordinal + reach - first) % reach) < max_lights;
						++ordinal;
					}
				}
				if (traced) {
					float3 light_bloom = float3(0.0f, 0.0f, 0.0f);
					const float3 point_light = ShadePoint(N, P, spec, bloom_scale, L, light_bloom) * light_scale;
					bloom += light_bloom * light_scale;
					if (shadow_ray_accumulate != 0) {
						lum_rt += point_light;
					}
					else {
						lum += point_light;
					}
				}
#else
				lum += ShadePoint(N, P, spec, bloom_scale, L, bloom);
#endif
			}
		}
	}
#ifdef DEFERRED_LIGHT_RAYS
	//A sampled pixel is written even when the lights it traced left it dark: the alpha is what keeps ShadowAccumulateCS from clamping
	//the history of its neighbourhood to values that do not hold the lights that were left out (see `sampled`).
	if (shadow_ray_accumulate != 0 && (sampled || any(lum_rt != 0.0f))) {
		//Everything else of the map is zero: it was cleared before the dispatch.
		//The alpha tells ShadowAccumulateCS that this pixel's value is one frame's share of its lights, scaled up.
		rt_shadow_out[px] = float4(lum_rt * weight, sampled ? 1.0f : 0.0f);
	}
#endif
#endif

#ifdef SR_STATS
	if (sr_stat[0] + sr_stat[1] > 0u) {
		sr_stat[7] = 1u;
		[unroll]
		for (uint k = 0; k < 8; ++k) {
			if (sr_stat[k] > 0u) {
				uint old;
				sr_counters.InterlockedAdd(k * 4, sr_stat[k], old);
			}
		}
	}
#endif
	AddToMaps(px, lum, bloom, weight, model);
}
