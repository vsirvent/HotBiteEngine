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
