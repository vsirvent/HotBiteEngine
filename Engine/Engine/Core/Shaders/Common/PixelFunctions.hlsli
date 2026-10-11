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

#ifndef __PIXEL_FUNCTIONS_INCLUDED__
#define __PIXEL_FUNCTIONS_INCLUDED__


#include "PixelCommon.hlsli"
#include "FastNoise.hlsli"
#include "QuickNoise.hlsli"
#include "NoiseSimplex.hlsli"
#include "RGBANoise.hlsli"
#include "MultiTexture.hlsli"

Texture2D diffuseTexture;
Texture2D normalTexture;
Texture2D specularTexture;
Texture2D aoTexture;
Texture2D armTexture;
Texture2D emissionTexture;
Texture2D opacityTexture;
Texture2D meshNormalTexture;
Texture2D highTexture;

Texture2D<float> depthTexture;
//What used to live between the texture declarations above and GetHeight below - the
//directional and point shadow lookups, CalcAmbient/CalcDirectional/CalcPoint (and their
//"WithoutNormal" variants for particles), the volumetric march and the light glow - is gone
//from this file. No material shader evaluates a light any more: the G-buffer pass writes
//what the surface is and DeferredLightCS (Common/DeferredLighting.hlsli) shades it, over
//the lights the tile masks hold. The shadow lookups are in Common/LightShadows.hlsli, the
//fog in Common/VolumetricLights.hlsli and the glow in Common/LightGlow.hlsli.

float GetHeight(float2 pos, const float calculated_values[MAX_MULTI_TEXTURE]) {
	float h = 0.0f;
	if (multi_texture_count > 0) {
		h = getMutliTextureValueLevel(basicSampler, 2, MULTITEXT_DISP, multi_texture_count, multi_texture_operations,
			calculated_values, multi_texture_uv_scales, pos, multi_highTexture).r;
	}
	else {
		h = highTexture.SampleLevel(basicSampler, pos, 0).r;
	}
	return h;
}

float2 CalculateDepthUVBinary(float scale, float base_steps, float angle_steps, float2 uv, float3 camera_pos,
	float3 fragment_pos, const float calculated_values[MAX_MULTI_TEXTURE], out float h, out float3 displacement) {

	float3 ToCam = camera_pos - fragment_pos;
	float dist = length(ToCam);
	float angle = dot(normalize(ToCam), float3(0.0f, 0.0f, 1.0f));
	float nsteps = base_steps + angle_steps * saturate(1.0f - angle) + clamp(lerp(base_steps, 0.0f, dist / 50.0f), 0.0, base_steps);
	int insteps = (int)nsteps;
	
	displacement = float3(0.0f, 0.0f, 0.0f);
	ToCam.xy *= scale * angle;
	ToCam = normalize(ToCam);
	ToCam.z = abs(ToCam.z);
	float3 ToCam2 = ToCam / ToCam.z;
	float3 curr_uvw = float3(uv, 0.0f) + ToCam2;
	int steps = 0;
	bool binary_enabled = false;
	float step_delta = 0.03f;
	int max_linear_steps = 20;
	int linear_steps = 0;
	while (steps <= insteps) {		
		h = GetHeight(curr_uvw.xy, calculated_values).r;
		if (abs(h - curr_uvw.z) < 0.005f)
		{
			break;
		}
		if (linear_steps++ > max_linear_steps) {
			binary_enabled = true;
		}
		if (h >= curr_uvw.z) {
			curr_uvw += ToCam * step_delta;
			displacement += ToCam * step_delta;
			binary_enabled = true;
		}
		else {
			curr_uvw -= ToCam * step_delta;
			displacement -= ToCam * step_delta;
		}
		if (binary_enabled) {
			steps++;
			step_delta *= 0.5f;
		}
	}
	displacement.xy /= scale;
	return curr_uvw.xy;
}

#endif