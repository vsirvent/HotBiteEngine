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

//Shades the G-buffer with light.
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
}

Texture2D<float4> positionTexture : register(t0);
//rt_ray_sources1: the world normal in xyz.
Texture2D<float4> normalTexture : register(t1);
Texture2D<float4> materialTexture : register(t2);
StructuredBuffer<PackedLight> lightsBuffer : register(t3);
ByteAddressBuffer tileMasks : register(t4);

RWTexture2D<float4> light_out : register(u0);
RWTexture2D<float4> bloom_out : register(u1);

#include "../Common/DeferredLighting.hlsli"

[numthreads(TILE_SIZE, TILE_SIZE, 1)]
void main(uint3 gid : SV_GroupID, uint3 dtid : SV_DispatchThreadID)
{
	if (dtid.x >= (uint)screenW || dtid.y >= (uint)screenH) {
		return;
	}
	const uint2 px = dtid.xy;
	const float4 wp = positionTexture[px];
	//Nothing was drawn here: the G-buffer is cleared to FLT_MAX.
	if (wp.x >= 1.0e30f) {
		return;
	}
	const float4 mat = materialTexture[px];
	const uint code = (uint)(mat.w + 0.5f);
	const uint model = code & GBUFFER_MODEL_MASK;
	if (pass_filter == 1 && (code & GBUFFER_PASS2) == 0) {
		return;
	}
	if (pass_filter == 2 && model != SHADING_SPLAT) {
		return;
	}

	const float3 P = wp.xyz;
	float3 N = normalTexture[px].xyz;
	float3 lum = float3(0.0f, 0.0f, 0.0f);
	float3 bloom = float3(0.0f, 0.0f, 0.0f);
	float weight = 1.0f;

	const uint tile = gid.y * (uint)tiles_x + gid.x;
	const uint mask_base = tile * TILE_MASK_WORDS * 4;
	int i;

	if (model == SHADING_WATER) {
		//The ray source normal is deliberately flattened (it is what reflections follow);
		//the shading normal, wave bumps and all, travels in the material map.
		const float ny = sqrt(max(0.0f, 1.0f - mat.y * mat.y - mat.z * mat.z));
		N = float3(mat.y, ny, mat.z);
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
		weight = 1.0f;
		lum *= (1.0f - exp(-mat.x * WATER_EXTINCTION));
	}
	else if (model == SHADING_LAVA) {
		N = float3(0.0f, 1.0f, 0.0f);
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
	}
	else {
		//STANDARD and SPLAT share the model; they differ in what `z` weights.
		const float spec = mat.x;
		const float bloom_scale = mat.y;
		const float sun = (model == SHADING_STANDARD) ? mat.z : 1.0f;
		if (model == SHADING_SPLAT) {
			weight = mat.z;
		}
		lum = ShadeAmbient(N);
		for (i = 0; i < dirLightsCount; ++i) {
			lum += ShadeDirectional(N, float4(P, 1.0f), spec, bloom_scale, dirLights[i],
			                        cloud_density, i, bloom) * ((i == 0) ? sun : 1.0f);
		}
		for (uint w = 0; w < TILE_MASK_WORDS; ++w) {
			uint bits = tileMasks.Load(mask_base + w * 4);
			while (bits != 0u) {
				const uint b = firstbitlow(bits);
				bits &= bits - 1u;
				lum += ShadePoint(N, P, spec, bloom_scale, lightsBuffer[w * 32u + b], bloom);
			}
		}
	}

	const float4 l = light_out[px];
	light_out[px] = float4(l.rgb + lum * weight, l.a);

	if (any(bloom != 0.0f)) {
		const float4 bl = bloom_out[px];
		//A cloud's own bloom was never clamped; everything else always was.
		const float3 nb = (model == SHADING_SPLAT) ? (bl.rgb + bloom * weight)
		                                           : saturate(bl.rgb + bloom);
		bloom_out[px] = float4(nb, 1.0f);
	}
}
