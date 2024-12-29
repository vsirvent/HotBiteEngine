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

#ifndef __MULTITEXTURE_HLSLI__
#define __MULTITEXTURE_HLSLI__

#include "Defines.hlsli"
#include "PixelCommon.hlsli"
#include "QuickNoise.hlsli"
#include "NoiseSimplex.hlsli"
#include "RGBANoise.hlsli"

//bits 0:2 defines the texture operation
#define MULTITEXT_OP_MASK 3
#define MULTITEXT_MIX 1
#define MULTITEXT_ADD 2
#define MULTITEXT_MULT 3

//bits 3:12 are reserved for the map texture enabled
#define MULTITEXT_DIFF (1 << 3)
#define MULTITEXT_NORM (1 << 4)
#define MULTITEXT_SPEC (1 << 5)
#define MULTITEXT_ARM  (1 << 6)
#define MULTITEXT_DISP (1 << 7)
#define MULTITEXT_AO   (1 << 8)
#define MULTITEXT_MASK (1 << 9)

//bits 12:15 reserved for texture flags
#define MULTITEXT_UV_NOISE (1 << 12)
#define MULTITEXT_MASK_NOISE (1 << 13)

Texture2D multi_diffuseTexture[MAX_MULTI_TEXTURE];
Texture2D multi_normalTexture[MAX_MULTI_TEXTURE];
Texture2D multi_specularTexture[MAX_MULTI_TEXTURE];
Texture2D multi_aoTexture[MAX_MULTI_TEXTURE];
Texture2D multi_armTexture[MAX_MULTI_TEXTURE];
Texture2D multi_highTexture[MAX_MULTI_TEXTURE];
Texture2D<float> multi_maskTexture[MAX_MULTI_TEXTURE];

//Packed array
static const uint multi_texture_operations[MAX_MULTI_TEXTURE] = (const uint[MAX_MULTI_TEXTURE])packed_multi_texture_operations;
static const float multi_texture_values[MAX_MULTI_TEXTURE] = (const float[MAX_MULTI_TEXTURE])packed_multi_texture_values;
static const float multi_texture_uv_scales[MAX_MULTI_TEXTURE] = (const float[MAX_MULTI_TEXTURE])packed_multi_texture_uv_scales;

void getValues(out float output_values[MAX_MULTI_TEXTURE], SamplerState basicSampler, float2 uv, uint count, const uint op[MAX_MULTI_TEXTURE], const Texture2D<float> mask[MAX_MULTI_TEXTURE], const float val[MAX_MULTI_TEXTURE], const float3 world_position) {
	for (uint i = 0; i < count; ++i) {
		//If texture is enabled
		if ((op[i] & MULTITEXT_OP_MASK) != 0) {
			float m = 1.0f;
			if ((op[i] & MULTITEXT_MASK) != 0) {
				m = mask[i].SampleLevel(basicSampler, uv, 0).r;
			}
			if ((op[i] & MULTITEXT_MASK_NOISE) != 0) {
				const float n = rgba_tnoise(world_position*0.1f + nrand(i));
				m *= n;
			}
			output_values[i] = val[i] * m;
		}
	}
}

float4 getMutliTextureValue(SamplerState basicSampler, uint type, uint count, const uint op[MAX_MULTI_TEXTURE], const float val[MAX_MULTI_TEXTURE],
	const float uv_scales[MAX_MULTI_TEXTURE], float2 uv, const Texture2D text[MAX_MULTI_TEXTURE]) {
	float3 color = { 0.0f, 0.0f, 0.0f };
	uint i;
	bool enabled = false;

	for (i = 0; i < count; ++i) {
		//If texture is enabled
		if ((op[i] & type) != 0) {
			enabled = true;
			if (val[i] > 0.0f) {
				float v = val[i];
				float uv_scale = uv_scales[i];
				float3 tcolor = text[i].Sample(basicSampler, uv * uv_scale).rgb;
				switch (op[i] & MULTITEXT_OP_MASK) {
				case MULTITEXT_MIX: {
					color = (1.0f - v) * color + v * tcolor;
				}break;
				case MULTITEXT_ADD: {
					color += v * tcolor;
				}break;
				case MULTITEXT_MULT: {
					color *= v * tcolor;
				}break;
				}
			}
		}
	}

	if (!enabled) {
		switch (type) {
		case MULTITEXT_AO: {
			color.rgb = 1.0f; //default is no occlussion (1.0)
		}break;
		case MULTITEXT_ARM: {
			color.r = 1.0f; //default is no occlussion (1.0), no roughness (0.0), no metallic (0.0)
		}break;
		}
	}
	return float4(color, 1.0f);
}

float3 getMutliTextureValueLevel(SamplerState basicSampler, uint level, uint type,
	uint count, const uint op[MAX_MULTI_TEXTURE], const float val[MAX_MULTI_TEXTURE],
	const float uv_scales[MAX_MULTI_TEXTURE], float2 uv, const Texture2D text[MAX_MULTI_TEXTURE]) {

	float3 color = { 0.0f, 0.0f, 0.0f };
	uint i;
	bool enabled = false;

	for (i = 0; i < count; ++i) {
		//If texture is enabled
		if ((op[i] & type) != 0) {
			enabled = true;
			if (val[i] > 0.0f) {
				float v = val[i];
				float uv_scale = uv_scales[i];
				float3 tcolor = text[i].SampleLevel(basicSampler, uv * uv_scale, level).rgb;
				switch (op[i] & MULTITEXT_OP_MASK) {
				case MULTITEXT_MIX: {
					color = (1.0f - v) * color + v * tcolor;
				}break;
				case MULTITEXT_ADD: {
					color += v * tcolor;
				}break;
				case MULTITEXT_MULT: {
					color *= v * tcolor;
				}break;
				}
			}
		}
	}

	if (!enabled) {
		switch (type) {
		case MULTITEXT_AO: {
			color.rgb = 1.0f; //default is no occlussion (1.0)
		}break;
		case MULTITEXT_ARM: {
			color.r = 1.0f; //default is no occlussion (1.0), no roughness (0.0), no metallic (0.0)
		}break;
		}
	}
	return color;
}

#endif