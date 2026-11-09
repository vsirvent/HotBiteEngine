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

//Light scattered by fog along the view ray: the directional lights' (with their shadows and
//the cloud shadow) and the point lights' that carry a volumetric density. Moved out of
//PixelFunctions.hlsli, which no longer holds any light code, and onto the aux lights - the
//march is per pixel and has no screen tile to take a light mask from, so it gets the nearest
//few lights that have a density (RenderSystem::PrepareLights).
//
//The includer supplies what LightShadows.hlsli does, plus cameraPosition, cameraDirection
//and cloud_density.

#ifndef __VOLUMETRIC_LIGHTS_HLSLI__
#define __VOLUMETRIC_LIGHTS_HLSLI__

#include "TiledLights.hlsli"
#include "LightShadows.hlsli"

#define max_nsteps 100

float3 DirVolumetricLight(float4 position, DirLight light, int index, float time, float cloud_density) {

	float3 lcolor = light.Color.rgb * light.intensity;
	float3 color = { 0.f, 0.f, 0.f };

	float step = 0.5f;
	float max_vol = 1.0f;

	float3 ToEye = cameraPosition.xyz - position.xyz;
	float ToEyeDist = length(ToEye);
	int nsteps = ToEyeDist / step;
	float3 ToEyeRayUnit = ToEye / nsteps;

	float3 camDir = cameraDirection.xyz;
	float angle_extra = saturate(pow(dot(normalize(camDir), normalize(light.DirToLight)), 3.0f)) * 2.0f;
	float density_step = light.density * step;
	float shadow, fog, att;
	int apply_fog = light.flags & DIR_LIGHT_FLAG_FOG;
	float3 step_color = (lcolor * density_step);	
	float3 step_color2 = step_color;
	
	//fog height
	float max_h = 80.0f;

	float3 ToLight = { 0.0f, 0.0f, 0.0f };
	float DistToLight = 0.0f;
	float LightRange = 0.0f;
	float density_y = 1.0f;
	if (light.range > 0.0f) {
		ToLight = light.position - position.xyz;
		ToLight *= -light.DirToLight;
		DistToLight = length(ToLight);
		LightRange = (light.range - DistToLight) / light.range;
		density_y = pow(saturate(LightRange), 2.0f);
	}
	if (density_y > 0.0f) {

		//End if maximum volume achieved
		float mag = 0.0f;
		float n = 1.0f;

		if (nsteps > max_nsteps) {
			int diff = nsteps - max_nsteps;
			position.xyz += ToEyeRayUnit * diff;
			nsteps = max_nsteps;
		}
		
		for (int i = 0; i < nsteps; ++i) {			
			shadow = 1.0f;
			fog = 1.0f;
			att = 1.0f;
			position.xyz += ToEyeRayUnit;
			if (apply_fog && max_h < position.y) {
				if (ToEyeRayUnit.y < 0.0f) {
					continue;
				}
				else {
					break;
				}
			}
			
			if (light.range > 0.0f) {
				float3 ToLight = light.position - position.xyz;
				ToLight *= -light.DirToLight;
				float DistToLight = length(ToLight);				
				float LightRange = (light.range - DistToLight) / light.range;
				att *= pow(saturate(LightRange), 2.0f);
				if (att == 0.0f) {
					break;
				}
			}
			if (apply_fog != 0) {
				float3 p = float3(position.x + time*0.5, position.y, position.z + time * 0.3f);
				float3 p2 = float3(position.x + time, position.y, position.z + time * 0.8f);
				float n = rgba_tnoise(p) * 0.5f +
					      rgba_tnoise(p*2.0f) * 0.3f +
						  rgba_tnoise(p2*4.0f) * 0.2f;
				n *= pow(saturate((max_h - position.y) / max_h), 5.0f);
				fog = clamp(30.0f * (n - 0.2f), -1.0f, 30.0f);
			}
			if (light.cast_shadow) {
				shadow = DirShadowPCFFASTAll(position, light, index);
				if (cloud_density > 0.0f) {
					shadow -= CloudPCF(position, light, cloud_density);
				}
				shadow = saturate(shadow);
			}
			att *= shadow;
			color += step_color * clamp(att * fog, 0.0f, 30.0f);
			mag = max(color.r, max(color.g, color.b));
			if (mag > max_vol) {
				break;
			}
		}
	}

	return color;
}

//A directional light's shafts: the same scattering as DirVolumetricLight, marched the whole way from the eye to the surface instead of
//the last 50 units next to the eye. The old march starts at the surface and skips the far part of a long ray, so what it sums is the air
//high over the ground, which every pixel sees fully lit: a uniform veil that saturates, with the shadows of buildings, trees and clouds
//(near the ground) never in it. Here:
// - the air thins out exponentially above `base` (scale height `height`), so the light is scattered near the ground, where the shadows
//   fall, and a pixel looking down through tall air is not burnt;
// - a fixed number of steps spread over the ray with a per-pixel offset (no banding, and a long ray costs no more than a short one);
// - Beer-Lambert: the light scattered at a step is dimmed by the air between it and the eye, and never saturates;
// - the shadow map's occlusion, and the clouds' (below), which is what makes the shafts: the sun reaches a point of air through the
//   gaps of a cloud layer, so the lit air is in beams that run along the sun's direction;
// - a Henyey-Greenstein phase (`anisotropy` 0 is even, more glows when looking towards the sun);
// - a soft cap, `max_light`: the light scattered saturates smoothly towards it instead of burning to white.
//The light's `density` is the scattering per world unit at `base` over 40 (so 1 in the light's own units is 0.04 a unit).

struct ShaftParams
{
	//The scale height of the scattering air over `base`, the phase's anisotropy, the soft cap and the number of steps.
	float height;
	float base;
	float anisotropy;
	float max_light;
	int steps;
	//The clouds: how strongly they shade the shafts (0 none, 1 all of the sun), how big they are, how sharp their edges are (the width of the
	//edge, in noise units: small makes clear beams), the height of their layer, how they drift (world units a second) and the time they drift by.
	float cloud_strength;
	float cloud_scale;
	float cloud_edge;
	float cloud_height;
	float2 cloud_drift;
	float cloud_time;
};

//Value noise on the lattice, three octaves, 0..1 around 0.5. Written out here (no texture) so the game's mist shader (Survival's
//SurvivalAtmospherePS.hlsl) can use the very same clouds: keep the two in step.
float ShaftHash(float3 p)
{
	p = frac(p * 0.3183099f + 0.1f);
	p *= 17.0f;
	return frac(p.x * p.y * p.z * (p.x + p.y + p.z));
}

float ShaftNoise(float3 x)
{
	float3 i = floor(x);
	float3 f = frac(x);
	f = f * f * (3.0f - 2.0f * f);
	float n000 = ShaftHash(i);
	float n100 = ShaftHash(i + float3(1.0f, 0.0f, 0.0f));
	float n010 = ShaftHash(i + float3(0.0f, 1.0f, 0.0f));
	float n110 = ShaftHash(i + float3(1.0f, 1.0f, 0.0f));
	float n001 = ShaftHash(i + float3(0.0f, 0.0f, 1.0f));
	float n101 = ShaftHash(i + float3(1.0f, 0.0f, 1.0f));
	float n011 = ShaftHash(i + float3(0.0f, 1.0f, 1.0f));
	float n111 = ShaftHash(i + float3(1.0f, 1.0f, 1.0f));
	return lerp(lerp(lerp(n000, n100, f.x), lerp(n010, n110, f.x), f.y),
		lerp(lerp(n001, n101, f.x), lerp(n011, n111, f.x), f.y), f.z);
}

float ShaftFbm(float3 p)
{
	float n = ShaftNoise(p) * 0.5f;
	n += ShaftNoise(p * 2.03f + 7.1f) * 0.3f;
	n += ShaftNoise(p * 4.01f + 3.7f) * 0.2f;
	return n;
}

//1 where a cloud of the layer shades the point from the sun, 0 where the sun reaches it. The line from the point to the sun crosses the layer
//at one place (so every point of a beam is shaded or lit together: the beams run along the sun's direction); there a noise, thresholded by the
//day's cover (`cover` 0 clear to 1 overcast) with an edge of `edge`, says whether there is cloud.
float ShaftCloudShadow(float3 p, float3 to_sun, float cover, ShaftParams s)
{
	float k = (s.cloud_height - p.y) / max(to_sun.y, 0.05f);
	float2 hit = p.xz + to_sun.xz * k;
	float2 q = (hit - s.cloud_drift * s.cloud_time) / max(s.cloud_scale, 1.0f);
	float n = ShaftFbm(float3(q, s.cloud_time * 0.01f));
	float threshold = lerp(0.72f, 0.38f, saturate(cover));
	return smoothstep(threshold - s.cloud_edge, threshold + s.cloud_edge, n);
}

float3 DirShaftLight(float4 position, DirLight light, int index, float cloud_cover, float2 pixel, ShaftParams s)
{
	float3 lcolor = light.Color.rgb * light.intensity;
	float3 eye = cameraPosition.xyz;
	float3 to_surface = position.xyz - eye;
	float dist = length(to_surface);
	if (dist < 0.01f) {
		return float3(0.0f, 0.0f, 0.0f);
	}
	float3 ray = to_surface / dist;
	float3 to_sun = normalize(light.DirToLight);
	int steps = clamp(s.steps, 4, 128);
	float dt = dist / (float)steps;
	float jitter = frac(52.9829189f * frac(dot(pixel, float2(0.06711056f, 0.00583715f))));

	//Looking towards the sun lights the air more.
	float g = clamp(s.anisotropy, 0.0f, 0.95f);
	float c = dot(ray, to_sun);
	float phase = (1.0f - g * g) / pow(max(1.0f + g * g - 2.0f * g * c, 0.0001f), 1.5f);

	//(The light's density is stored as thousandths of the value it was given.)
	float sigma0 = light.density * 40.0f;
	float transmit = 1.0f;
	float3 scattered = float3(0.0f, 0.0f, 0.0f);
	for (int i = 0; i < steps; ++i) {
		float t = ((float)i + jitter) * dt;
		float4 p = float4(eye + ray * t, 1.0f);
		float above = max(p.y - s.base, 0.0f);
		float sigma = sigma0 * exp(-above / max(s.height, 0.01f));
		float visible = 1.0f;
		if (light.cast_shadow) {
			visible = DirShadowPCFFASTAll(p, light, index);
		}
		if (s.cloud_strength > 0.0f) {
			visible *= 1.0f - saturate(s.cloud_strength) * ShaftCloudShadow(p.xyz, to_sun, cloud_cover, s);
		}
		float taken = 1.0f - exp(-sigma * dt);
		scattered += transmit * taken * visible * phase * lcolor;
		transmit *= 1.0f - taken;
	}
	float cap = max(s.max_light, 0.0001f);
	return cap * (1.0f - exp(-scattered / cap));
}

float3 VolumetricLight(float3 position, AuxLight aux)
{
	const PackedLight light = aux.light;
	const float3 light_color = UnpackLightColor(light);
	float3 color = { 0.f, 0.f, 0.f };
	float3 step_color = light_color;
	float step = 0.5f;
	float max_vol = 0.3f;
	step_color.rgb *= (aux.density * step);
	float3 intersection_position;

	if (line_sphere_intersection(position, cameraPosition.xyz, light.position, light.range, intersection_position)) {
		position = intersection_position;
		float3 lposition = light.position;
		float3 ToEye = cameraPosition.xyz - position;
		float ToEyeDist = length(ToEye);
		int nsteps = ToEyeDist / step;
		float3 ToEyeRayUnit = ToEye / nsteps;
		float extra_step_w = ((ToEyeDist / step) - (float)nsteps);
		float mag = 0.0f;

		if (nsteps > max_nsteps) {
			int diff = nsteps - max_nsteps;
			position += ToEyeRayUnit * diff;
			nsteps = max_nsteps;
		}
		const int shadow_slot = LightShadowSlot(light);
		for (int i = 0; i <= nsteps; ++i) {
			position += ToEyeRayUnit;
			float3 ToLight = position - lposition;
			float ToLightDist = length(ToLight);
			if (ToLightDist > light.range) {
				break;
			}
			float LightRange = (light.range - ToLightDist) / light.range;
			float DistToLightNorm = saturate(LightRange * LightRange);
			float shadow = 1.0f;
			if (shadow_slot >= 0) {
				shadow = PointShadowPCFFast(ToLight, shadow_slot);
			}
			float att = DistToLightNorm * PackedSpotFactor(light, ToLight);
			att *= shadow;
			saturate(att);
			color += step_color * att;
			mag = max(color.r, max(color.g, color.b));
			if (mag > max_vol) {
				break;
			}
		}
	}
	return color;
}

#endif
